#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"
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
        auto res_type = RankedTensorType::get(res_shape, src_type.getElementType());

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
                         LowerTensorTranspose>(ctx);
            // Only run if there are tensor ops in this func.
            bool has_tensor = false;
            for (Block& block : op.getRegion(0)) {
                for (Operation& nested : block.getOperations()) {
                    if (isa<TensorMatmulOp, TensorAddOp, TensorMulOp,
                            TensorTransposeOp, TensorFillOp>(nested)) {
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
