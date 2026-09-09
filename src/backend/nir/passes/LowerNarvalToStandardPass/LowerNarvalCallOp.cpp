#include "backend/nir/NarvalOps.h"
#include "backend/nir/passes/NarvalTypeConverter.hpp"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

struct LowerNarvalCallOp : public OpConversionPattern<CallOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(CallOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        SmallVector<Type> rt;
        if (failed(typeConverter->convertTypes(op.getResultTypes(), rt)))
            return failure();

        // Update the function signature to the converted types — but only for
        // runtime declarations. A user-defined function (with a body) keeps its
        // narval-typed signature here: erasing it would destroy the body, and
        // LowerNarvalToLLVMPass converts signature + body together in phase B.
        auto mod = op->getParentOfType<ModuleOp>();
        if (auto fn = mod.lookupSymbol<func::FuncOp>(op.getCallee())) {
            if (fn.isExternal()) {
                SmallVector<Type> param_types;
                for (auto v : a.getOperands()) param_types.push_back(v.getType());
                auto new_ft = FunctionType::get(r.getContext(), param_types, rt);
                if (fn.getFunctionType() != new_ft) {
                    fn.erase();
                    ensure_decl(mod, r, op.getCallee().str(), new_ft);
                }
            }
        }

        r.replaceOpWithNewOp<func::CallOp>(op, rt, op.getCallee(), a.getOperands());
        return success();
    }
};

} // namespace

void populateLowerNarvalCallOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc) {
    patterns.add<LowerNarvalCallOp>(tc, patterns.getContext());
}

} // namespace nv
