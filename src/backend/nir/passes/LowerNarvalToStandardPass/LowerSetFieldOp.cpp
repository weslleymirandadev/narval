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
// narval.set_field → nv_set_field
//===----------------------------------------------------------------------===//

struct LowerSetFieldOp : public OpConversionPattern<SetFieldOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(SetFieldOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        auto loc  = op.getLoc();
        auto* ctx = r.getContext();
        auto ptr  = LLVM::LLVMPointerType::get(ctx);
        auto mod  = op->getParentOfType<ModuleOp>();
        {
            auto sf_ft = FunctionType::get(ctx, {ptr, ptr, ptr}, {});
            if (auto f = mod.lookupSymbol<func::FuncOp>("nv_set_field")) {
                if (f.getFunctionType() != sf_ft) f.erase();
            }
            ensure_decl(mod, r, "nv_set_field", sf_ft);
        }
        auto key = str_ptr(mod, r, loc, op.getFieldName());
        func::CallOp::create(r, loc, TypeRange{}, "nv_set_field",
                              ValueRange{adaptor.getObject(), key,
                                        adaptor.getValue()});
        r.eraseOp(op);
        return success();
    }
};

} // namespace

void populateLowerSetFieldOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc) {
    patterns.add<LowerSetFieldOp>(tc, patterns.getContext());
}

} // namespace nv
