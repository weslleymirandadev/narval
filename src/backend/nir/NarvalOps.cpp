#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalDialect.h"
#include "backend/nir/NarvalTypes.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/PatternMatch.h"

using namespace mlir;
using namespace mlir::narval;

// Pull in the generated op definitions.
#define GET_OP_CLASSES
#include "NarvalOps.cpp.inc"

//===----------------------------------------------------------------------===//
// IfOp — verifier
//===----------------------------------------------------------------------===//

LogicalResult IfOp::verify() {
    // If results are expected, both then and else regions must be non-empty.
    if (!getResults().empty() && (getElseRegion().empty() ||
                                   getElseRegion().front().empty()))
        return emitOpError("requires an else region when yielding results");
    return success();
}

//===----------------------------------------------------------------------===//
// ForRangeOp — verifier
//===----------------------------------------------------------------------===//

LogicalResult ForRangeOp::verify() {
    if (!getLb().getType().isIndex() || !getUb().getType().isIndex() ||
        !getStep().getType().isIndex())
        return emitOpError("lb, ub, and step must be index type");
    return success();
}

//===----------------------------------------------------------------------===//
// ConstantOp — folder
//===----------------------------------------------------------------------===//

OpFoldResult ConstantOp::fold(FoldAdaptor /*adaptor*/) {
    return getValue();
}

//===----------------------------------------------------------------------===//
// ComptimeConstOp — folder + canonicalizer
//===----------------------------------------------------------------------===//

OpFoldResult ComptimeConstOp::fold(FoldAdaptor /*adaptor*/) {
    return getValue();
}

namespace {
struct FoldComptimeConst : public OpRewritePattern<ComptimeConstOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(ComptimeConstOp op,
                                  PatternRewriter& rewriter) const override {
        rewriter.replaceOpWithNewOp<ConstantOp>(
            op, op.getResult().getType(), op.getValue());
        return success();
    }
};
} // namespace

void ComptimeConstOp::getCanonicalizationPatterns(RewritePatternSet& patterns,
                                                   MLIRContext* ctx) {
    patterns.add<FoldComptimeConst>(ctx);
}

//===----------------------------------------------------------------------===//
// ComptimeCallOp — canonicalizer
//===----------------------------------------------------------------------===//

namespace {
struct FoldComptimeCallWithValue : public OpRewritePattern<ComptimeCallOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(ComptimeCallOp op,
                                  PatternRewriter& rewriter) const override {
        if (op.getNumResults() != 1)
            return failure();

        Attribute value = op->getAttr("value");
        if (!value)
            value = op->getAttr("result");
        if (!value)
            return failure();

        rewriter.replaceOpWithNewOp<ComptimeConstOp>(
            op, op.getResult(0).getType(), value);
        return success();
    }
};
} // namespace

void ComptimeCallOp::getCanonicalizationPatterns(RewritePatternSet& patterns,
                                                  MLIRContext* ctx) {
    patterns.add<FoldComptimeCallWithValue>(ctx);
}

//===----------------------------------------------------------------------===//
// MoveOp — canonicalizer
//===----------------------------------------------------------------------===//

namespace {
struct FoldDoubleMove : public OpRewritePattern<MoveOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(MoveOp op,
                                  PatternRewriter& rewriter) const override {
        if (auto inner = op.getSource().getDefiningOp<MoveOp>()) {
            rewriter.replaceOpWithNewOp<MoveOp>(op, op.getType(),
                                                inner.getSource());
            return success();
        }
        return failure();
    }
};
} // namespace

// narval.move(narval.borrow(x)) → x
// A move of a borrow is equivalent to the original value (no ownership change needed).
namespace {
struct FoldMoveOfBorrow : public OpRewritePattern<MoveOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(MoveOp op,
                                  PatternRewriter& rewriter) const override {
        if (auto borrow = op.getSource().getDefiningOp<BorrowOp>()) {
            rewriter.replaceOp(op, borrow.getSource());
            return success();
        }
        return failure();
    }
};
} // namespace

void MoveOp::getCanonicalizationPatterns(RewritePatternSet& patterns,
                                          MLIRContext* ctx) {
    patterns.add<FoldDoubleMove, FoldMoveOfBorrow>(ctx);
}

//===----------------------------------------------------------------------===//
// TensorMatmulOp — verifier
//===----------------------------------------------------------------------===//

LogicalResult TensorMatmulOp::verify() {
    auto lhs = dyn_cast<RankedTensorType>(getLhs().getType());
    auto rhs = dyn_cast<RankedTensorType>(getRhs().getType());
    auto res = dyn_cast<RankedTensorType>(getResult().getType());

    if (!lhs || !rhs || !res)
        return emitOpError("requires ranked tensor operands and result");

    if (lhs.getRank() != 2 || rhs.getRank() != 2)
        return emitOpError("matmul requires rank-2 tensors (got lhs rank=")
               << lhs.getRank() << ", rhs rank=" << rhs.getRank() << ")";

    int64_t K1 = lhs.getDimSize(1);
    int64_t K2 = rhs.getDimSize(0);

    if (K1 != ShapedType::kDynamic && K2 != ShapedType::kDynamic && K1 != K2)
        return emitOpError()
               << "inner dimensions mismatch: lhs dim[1]=" << K1
               << " vs rhs dim[0]=" << K2;

    if (res.getRank() != 2)
        return emitOpError("result must be rank-2");

    int64_t M = lhs.getDimSize(0);
    int64_t N = rhs.getDimSize(1);

    if (M != ShapedType::kDynamic && res.getDimSize(0) != M)
        return emitOpError()
               << "result dim[0]=" << res.getDimSize(0)
               << " must equal lhs dim[0]=" << M;
    if (N != ShapedType::kDynamic && res.getDimSize(1) != N)
        return emitOpError()
               << "result dim[1]=" << res.getDimSize(1)
               << " must equal rhs dim[1]=" << N;

    return success();
}

//===----------------------------------------------------------------------===//
// TensorAddOp — canonicalizer + verifier
//===----------------------------------------------------------------------===//

namespace {
struct FoldTensorAddZeroFill : public OpRewritePattern<TensorAddOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(TensorAddOp op,
                                  PatternRewriter& rewriter) const override {
        auto fill = op.getRhs().getDefiningOp<TensorFillOp>();
        if (!fill) return failure();

        auto cst = fill.getFillValue().getDefiningOp<ConstantOp>();
        if (!cst) return failure();

        Attribute val = cst.getValue();
        bool is_zero = false;
        if (auto int_attr = dyn_cast<IntegerAttr>(val))
            is_zero = int_attr.getInt() == 0;
        else if (auto flt_attr = dyn_cast<FloatAttr>(val))
            is_zero = flt_attr.getValueAsDouble() == 0.0;

        if (is_zero) {
            rewriter.replaceOp(op, op.getLhs());
            return success();
        }
        return failure();
    }
};
} // namespace

void TensorAddOp::getCanonicalizationPatterns(RewritePatternSet& patterns,
                                               MLIRContext* ctx) {
    patterns.add<FoldTensorAddZeroFill>(ctx);
}

LogicalResult TensorAddOp::verify() {
    auto lhs = dyn_cast<RankedTensorType>(getLhs().getType());
    auto rhs = dyn_cast<RankedTensorType>(getRhs().getType());
    if (!lhs || !rhs)
        return emitOpError("requires ranked tensor operands");
    if (lhs.getRank() != rhs.getRank())
        return emitOpError("operand ranks must match: lhs=")
               << lhs.getRank() << " rhs=" << rhs.getRank();
    for (int i = 0; i < (int)lhs.getRank(); ++i) {
        int64_t d0 = lhs.getDimSize(i), d1 = rhs.getDimSize(i);
        if (d0 != ShapedType::kDynamic && d1 != ShapedType::kDynamic && d0 != d1)
            return emitOpError()
                   << "shape mismatch at dim " << i << ": " << d0 << " vs " << d1;
    }
    return success();
}

//===----------------------------------------------------------------------===//
// TensorMulOp — verifier
//===----------------------------------------------------------------------===//

LogicalResult TensorMulOp::verify() {
    auto lhs = dyn_cast<RankedTensorType>(getLhs().getType());
    auto rhs = dyn_cast<RankedTensorType>(getRhs().getType());
    if (!lhs || !rhs)
        return emitOpError("requires ranked tensor operands");
    if (lhs.getRank() != rhs.getRank())
        return emitOpError("operand ranks must match");
    return success();
}

//===----------------------------------------------------------------------===//
// TensorTransposeOp — canonicalizer
//===----------------------------------------------------------------------===//

namespace {
struct FoldDoubleTranspose : public OpRewritePattern<TensorTransposeOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(TensorTransposeOp op,
                                  PatternRewriter& rewriter) const override {
        auto inner = op.getInput().getDefiningOp<TensorTransposeOp>();
        if (!inner) return failure();

        auto p_outer = op.getPermutation();
        auto p_inner = inner.getPermutation();
        if (p_outer.size() != p_inner.size()) return failure();

        std::vector<int64_t> composed(p_outer.size());
        for (size_t i = 0; i < p_inner.size(); ++i)
            composed[i] = p_outer[p_inner[i]];

        bool is_identity = true;
        for (size_t i = 0; i < composed.size(); ++i)
            if (composed[i] != (int64_t)i) { is_identity = false; break; }

        if (is_identity) {
            rewriter.replaceOp(op, inner.getInput());
            return success();
        }
        return failure();
    }
};
} // namespace

void TensorTransposeOp::getCanonicalizationPatterns(RewritePatternSet& patterns,
                                                     MLIRContext* ctx) {
    patterns.add<FoldDoubleTranspose>(ctx);
}
