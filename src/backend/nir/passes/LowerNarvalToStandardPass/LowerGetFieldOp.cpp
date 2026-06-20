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

//===----------------------------------------------------------------------===//
// narval.get_field → nv_get_field
//===----------------------------------------------------------------------===//

struct LowerGetFieldOp : public OpConversionPattern<GetFieldOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(GetFieldOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        auto loc  = op.getLoc();
        auto* ctx = r.getContext();
        auto ptr  = LLVM::LLVMPointerType::get(ctx);
        auto mod  = op->getParentOfType<ModuleOp>();
        {
            auto gf_ft = FunctionType::get(ctx, {ptr, ptr}, {ptr});
            if (auto f = mod.lookupSymbol<func::FuncOp>("nv_get_field")) {
                if (f.getFunctionType() != gf_ft) f.erase();
            }
            ensure_decl(mod, r, "nv_get_field", gf_ft);
        }
        auto key = str_ptr(mod, r, loc, op.getFieldName());
        r.replaceOp(op,
            func::CallOp::create(r, loc, TypeRange{ptr}, "nv_get_field",
                                  ValueRange{adaptor.getObject(), key})
                .getResult(0));
        return success();
    }
};

} // namespace

void populateLowerGetFieldOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc) {
    patterns.add<LowerGetFieldOp>(tc, patterns.getContext());
}

} // namespace nv
