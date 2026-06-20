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

struct LowerDropOp : public OpConversionPattern<DropOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(DropOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        auto loc = op.getLoc(); auto* ctx = r.getContext();
        auto ptr = LLVM::LLVMPointerType::get(ctx);
        auto mod = op->getParentOfType<ModuleOp>();
        auto ft = FunctionType::get(ctx,{ptr},{});
        if (auto f = mod.lookupSymbol<func::FuncOp>("nv_free")) {
            if (f.getFunctionType() != ft) f.erase();
        }
        ensure_decl(mod, r, "nv_free", ft);
        func::CallOp::create(r, loc, TypeRange{}, "nv_free",
                              ValueRange{a.getValue()});
        r.eraseOp(op); return success();
    }
};

} // namespace

void populateLowerDropOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc) {
    patterns.add<LowerDropOp>(tc, patterns.getContext());
}

} // namespace nv
