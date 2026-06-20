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

struct LowerAllocOp : public OpConversionPattern<AllocOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(AllocOp op, OpAdaptor,
                                  ConversionPatternRewriter& r) const override {
        auto loc = op.getLoc(); auto* ctx = r.getContext();
        auto ptr = LLVM::LLVMPointerType::get(ctx);
        auto mod = op->getParentOfType<ModuleOp>();
        auto callee = op.getRuntimeCtor();
        auto ft = FunctionType::get(ctx,{}, {ptr});
        if (auto f = mod.lookupSymbol<func::FuncOp>(callee)) {
            if (f.getFunctionType() != ft) f.erase();
        }
        ensure_decl(mod, r, callee, ft);
        r.replaceOp(op, func::CallOp::create(r, loc, TypeRange{ptr},
                                              callee, ValueRange{})
                           .getResult(0));
        return success();
    }
};

} // namespace

void populateLowerAllocOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc) {
    patterns.add<LowerAllocOp>(tc, patterns.getContext());
}

} // namespace nv
