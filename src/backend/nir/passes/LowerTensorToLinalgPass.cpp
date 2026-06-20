#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"
#include "mlir/IR/AffineMap.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

#define GEN_PASS_DEF_LOWERNARVALTENSORPASS
#include "NarvalPasses.h.inc"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

// Helpers to create ranked tensor empty buffers.
static Value make_empty(PatternRewriter& r, Location loc,
                         RankedTensorType type) {
    SmallVector<int64_t> static_shape(type.getShape());
    return tensor::EmptyOp::create(r, loc, static_shape,
                                    type.getElementType()).getResult();
}

static LogicalResult inline_region_as_linalg_body(
    Region& source, OpBuilder& b, Location loc, ValueRange args) {
    if (source.empty() || source.front().empty())
        return failure();

    Block& src_block = source.front();
    if (src_block.getNumArguments() != args.size())
        return failure();

    IRMapping mapping;
    for (auto [from, to] : llvm::zip(src_block.getArguments(), args))
        mapping.map(from, to);

    for (Operation& nested : src_block.without_terminator())
        b.clone(nested, mapping);

    auto yield = dyn_cast<YieldOp>(src_block.getTerminator());
    if (!yield || yield.getOperands().empty())
        return failure();

    SmallVector<Value> yielded;
    for (Value value : yield.getOperands())
        yielded.push_back(mapping.lookupOrDefault(value));
    linalg::YieldOp::create(b, loc, yielded);
    return success();
}

//===----------------------------------------------------------------------===//
// narval.tensor_fill → tensor.empty + linalg.fill
//===----------------------------------------------------------------------===//

struct LowerTensorFill : public OpRewritePattern<TensorFillOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(TensorFillOp op,
                                  PatternRewriter& r) const override {
        auto res_type = mlir::dyn_cast<RankedTensorType>(op.getResult().getType());
        if (!res_type) return failure();
        auto loc    = op.getLoc();
        auto empty  = make_empty(r, loc, res_type);
        SmallVector<Value> inputs = {op.getFillValue()};
        SmallVector<Value> outputs = {empty};
        auto filled = linalg::FillOp::create(r, loc, inputs, outputs);
        r.replaceOp(op, filled.getResult(0));
        return success();
    }
};

//===----------------------------------------------------------------------===//
// narval.tensor_matmul → tensor.empty + linalg.fill(0) + linalg.matmul
//===----------------------------------------------------------------------===//

struct LowerTensorMatmul : public OpRewritePattern<TensorMatmulOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(TensorMatmulOp op,
                                  PatternRewriter& r) const override {
        auto res_type = mlir::dyn_cast<RankedTensorType>(op.getResult().getType());
        if (!res_type) return failure();
        auto loc = op.getLoc();

        // Zero-initialise the output tensor.
        auto empty = make_empty(r, loc, res_type);
        auto zero_attr = r.getZeroAttr(res_type.getElementType());
        auto zero = arith::ConstantOp::create(r, loc, zero_attr);
        SmallVector<Value> fill_in  = {zero.getResult()};
        SmallVector<Value> fill_out = {empty};
        auto init = linalg::FillOp::create(r, loc, fill_in, fill_out).getResult(0);

        SmallVector<Type>  types   = {res_type};
        SmallVector<Value> inputs  = {op.getLhs(), op.getRhs()};
        SmallVector<Value> outputs = {init};
        auto matmul = linalg::MatmulOp::create(r, loc, types, inputs, outputs);
        r.replaceOp(op, matmul.getResult(0));
        return success();
    }
};

//===----------------------------------------------------------------------===//
// narval.tensor_add → tensor.empty + linalg.add
//===----------------------------------------------------------------------===//

struct LowerTensorAdd : public OpRewritePattern<TensorAddOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(TensorAddOp op,
                                  PatternRewriter& r) const override {
        auto res_type = mlir::dyn_cast<RankedTensorType>(op.getResult().getType());
        if (!res_type) return failure();
        auto loc    = op.getLoc();
        auto empty  = make_empty(r, loc, res_type);
        SmallVector<Type>  types   = {res_type};
        SmallVector<Value> inputs  = {op.getLhs(), op.getRhs()};
        SmallVector<Value> outputs = {empty};
        auto add = linalg::AddOp::create(r, loc, types, inputs, outputs);
        r.replaceOp(op, add.getResult(0));
        return success();
    }
};

//===----------------------------------------------------------------------===//
// narval.tensor_mul → tensor.empty + linalg.mul
//===----------------------------------------------------------------------===//

struct LowerTensorMul : public OpRewritePattern<TensorMulOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(TensorMulOp op,
                                  PatternRewriter& r) const override {
        auto res_type = mlir::dyn_cast<RankedTensorType>(op.getResult().getType());
        if (!res_type) return failure();
        auto loc    = op.getLoc();
        auto empty  = make_empty(r, loc, res_type);
        SmallVector<Type>  types   = {res_type};
        SmallVector<Value> inputs  = {op.getLhs(), op.getRhs()};
        SmallVector<Value> outputs = {empty};
        auto mul = linalg::MulOp::create(r, loc, types, inputs, outputs);
        r.replaceOp(op, mul.getResult(0));
        return success();
    }
};

//===----------------------------------------------------------------------===//
// narval.tensor_transpose → tensor.empty + linalg.transpose
//===----------------------------------------------------------------------===//

struct LowerTensorTranspose : public OpRewritePattern<TensorTransposeOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(TensorTransposeOp op,
                                  PatternRewriter& r) const override {
        auto src_type = mlir::dyn_cast<RankedTensorType>(op.getInput().getType());
        if (!src_type) return failure();
        auto loc  = op.getLoc();
        auto perm = op.getPermutation();

        // Compute result type by permuting dims.
        SmallVector<int64_t> res_shape(src_type.getRank());
        for (int i = 0; i < (int)perm.size(); ++i)
            res_shape[i] = src_type.getDimSize(perm[i]);
        auto empty = tensor::EmptyOp::create(r, loc, res_shape,
                                              src_type.getElementType());
        SmallVector<int64_t> perm_vec(perm.begin(), perm.end());
        linalg::TransposeOp::create(
            r, loc, op.getInput(), empty.getResult(), perm_vec);
        // linalg.transpose updates the output tensor in-place; the result is the init.
        r.replaceOp(op, empty.getResult());
        return success();
    }
};

//===----------------------------------------------------------------------===//
// narval.tensor_map → tensor.empty + linalg.generic
//===----------------------------------------------------------------------===//

struct LowerTensorMap : public OpRewritePattern<TensorMapOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(TensorMapOp op,
                                  PatternRewriter& r) const override {
        auto res_type = dyn_cast<RankedTensorType>(op.getResult().getType());
        if (!res_type) return failure();

        auto loc = op.getLoc();
        auto empty = make_empty(r, loc, res_type);
        auto map = AffineMap::getMultiDimIdentityMap(res_type.getRank(),
                                                     r.getContext());
        SmallVector<AffineMap> maps = {map, map};
        SmallVector<utils::IteratorType> iterators(
            res_type.getRank(), utils::IteratorType::parallel);

        auto generic = linalg::GenericOp::create(
            r, loc, TypeRange{res_type}, ValueRange{op.getInput()},
            ValueRange{empty}, maps, iterators,
            [&](OpBuilder& body_builder, Location body_loc, ValueRange args) {
                if (failed(inline_region_as_linalg_body(
                        op.getMapper(), body_builder, body_loc, args.take_front(1))))
                    linalg::YieldOp::create(body_builder, body_loc, args.front());
            });

        r.replaceOp(op, generic.getResults());
        return success();
    }
};

//===----------------------------------------------------------------------===//
// narval.tensor_reduce → linalg.reduce
//===----------------------------------------------------------------------===//

struct LowerTensorReduce : public OpRewritePattern<TensorReduceOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(TensorReduceOp op,
                                  PatternRewriter& r) const override {
        int64_t dim = static_cast<int64_t>(op.getDimension());
        auto reduce = linalg::ReduceOp::create(
            r, op.getLoc(), ValueRange{op.getInput()}, ValueRange{op.getInit()},
            ArrayRef<int64_t>{dim},
            [&](OpBuilder& body_builder, Location body_loc, ValueRange args) {
                SmallVector<Value> combiner_args;
                if (args.size() == 2) {
                    // linalg.reduce passes (element, accumulator); Narval's
                    // combiner region is specified as (accumulator, element).
                    combiner_args.push_back(args[1]);
                    combiner_args.push_back(args[0]);
                } else {
                    combiner_args.append(args.begin(), args.end());
                }
                if (failed(inline_region_as_linalg_body(
                        op.getCombiner(), body_builder, body_loc, combiner_args)))
                    linalg::YieldOp::create(body_builder, body_loc,
                                            args.size() > 1 ? args[1] : args.front());
            });
        r.replaceOp(op, reduce.getResults());
        return success();
    }
};

//===----------------------------------------------------------------------===//
// narval.tensor_slice → tensor.extract_slice
//===----------------------------------------------------------------------===//

struct LowerTensorSlice : public OpRewritePattern<TensorSliceOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(TensorSliceOp op,
                                  PatternRewriter& r) const override {
        auto res_type = dyn_cast<RankedTensorType>(op.getResult().getType());
        if (!res_type) return failure();
        auto slice = tensor::ExtractSliceOp::create(
            r, op.getLoc(), res_type, op.getSource(), op.getOffsets(),
            op.getSizes(), op.getStrides(), ArrayRef<NamedAttribute>{});
        r.replaceOp(op, slice.getResult());
        return success();
    }
};

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct LowerNarvalTensorPassImpl
    : public ::impl::LowerNarvalTensorPassBase<LowerNarvalTensorPassImpl> {

    void runOnOperation() override {
        ModuleOp module = getOperation();
        // Iterate top-level func ops without walk() to avoid TypeID-lookup issues.
        for (Operation& op : module.getBody()->getOperations()) {
            if (op.getName().getStringRef() != "func.func") continue;
            if (op.getNumRegions() == 0 || op.getRegion(0).empty()) continue;
            MLIRContext* ctx = &getContext();
            RewritePatternSet patterns(ctx);
            patterns.add<LowerTensorFill, LowerTensorMatmul,
                         LowerTensorAdd, LowerTensorMul,
                         LowerTensorTranspose, LowerTensorMap,
                         LowerTensorReduce, LowerTensorSlice>(ctx);
            // Only run if there are tensor ops in this func.
            bool has_tensor = false;
            for (Block& block : op.getRegion(0)) {
                for (Operation& nested : block.getOperations()) {
                    if (isa<TensorMatmulOp, TensorAddOp, TensorMulOp,
                            TensorTransposeOp, TensorFillOp, TensorMapOp,
                            TensorReduceOp, TensorSliceOp>(nested)) {
                        has_tensor = true;
                        break;
                    }
                }
                if (has_tensor) break;
            }
            if (!has_tensor) continue;
            if (failed(applyPatternsGreedily(&op, std::move(patterns)))) {
                op.emitError("lower-narval-tensor: pattern application failed");
                signalPassFailure();
                return;
            }
        }
    }
};

} // namespace

std::unique_ptr<mlir::Pass> createLowerNarvalTensorPass() {
    return std::make_unique<LowerNarvalTensorPassImpl>();
}

} // namespace nv
