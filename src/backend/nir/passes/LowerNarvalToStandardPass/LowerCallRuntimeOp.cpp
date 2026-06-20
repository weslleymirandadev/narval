#include "backend/nir/NarvalOps.h"
#include "backend/nir/passes/NarvalTypeConverter.hpp"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

struct LowerCallRuntimeOp : public OpConversionPattern<CallRuntimeOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(CallRuntimeOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        SmallVector<Type> rt;
        if (failed(typeConverter->convertTypes(op.getResultTypes(), rt)))
            return failure();

        // Collect the converted argument types from the adapted operands.
        SmallVector<Type> arg_types;
        for (auto v : a.getOperands()) arg_types.push_back(v.getType());

        // Re-declare the function with the fully-converted signature so the
        // resulting func::CallOp doesn't need a !narval.value to !llvm.ptr
        // materialization at the call site.
        auto mod = op->getParentOfType<ModuleOp>();
        auto ft  = FunctionType::get(r.getContext(), arg_types, rt);
        // Remove any prior declaration with incompatible signature so
        // ensure_decl creates the correct one.
        if (auto existing = mod.lookupSymbol<func::FuncOp>(op.getCallee())) {
            if (existing.getFunctionType() != ft)
                existing.erase();
        }
        ensure_decl(mod, r, op.getCallee().str(), ft);

        r.replaceOpWithNewOp<func::CallOp>(op, rt, op.getCallee(), a.getOperands());
        return success();
    }
};

} // namespace

void populateLowerCallRuntimeOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc) {
    patterns.add<LowerCallRuntimeOp>(tc, patterns.getContext());
}

} // namespace nv
