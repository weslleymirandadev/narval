#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalTypes.h"
#include "backend/nir/passes/NarvalTypeConverter.hpp"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

struct LowerCallOp : public OpConversionPattern<func::CallOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(func::CallOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        // Only handle func.call ops that have narval types that need conversion.
        bool needs_conversion = false;
        for (auto t : op.getOperandTypes())
            if (mlir::isa<narval::ValueType, narval::RefType, narval::MutRefType>(t))
                needs_conversion = true;
        for (auto t : op.getResultTypes())
            if (mlir::isa<narval::ValueType, narval::RefType, narval::MutRefType>(t))
                needs_conversion = true;
        if (!needs_conversion)
            return failure();

        SmallVector<Type> rt;
        if (failed(typeConverter->convertTypes(op.getResultTypes(), rt)))
            return failure();

        // Update the function signature to the converted types so the
        // resulting func::CallOp doesn't need a materialization cast.
        auto mod = op->getParentOfType<ModuleOp>();
        if (auto fn = mod.lookupSymbol<func::FuncOp>(op.getCallee())) {
            SmallVector<Type> param_types;
            for (auto v : a.getOperands()) param_types.push_back(v.getType());
            auto new_ft = FunctionType::get(r.getContext(), param_types, rt);
            if (fn.getFunctionType() != new_ft) {
                fn.erase();
                ensure_decl(mod, r, op.getCallee().str(), new_ft);
            }
        }

        r.replaceOpWithNewOp<func::CallOp>(op, rt, op.getCallee(), a.getOperands());
        return success();
    }
};

} // namespace

void populateLowerCallOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc) {
    patterns.add<LowerCallOp>(tc, patterns.getContext());
}

} // namespace nv
