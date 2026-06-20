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
// narval.new → nv_alloc_object + __ctor_ClassName
//===----------------------------------------------------------------------===//

struct LowerNewOp : public OpConversionPattern<NewOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(NewOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        auto loc  = op.getLoc();
        auto* ctx = r.getContext();
        auto ptr  = LLVM::LLVMPointerType::get(ctx);
        auto mod  = op->getParentOfType<ModuleOp>();
        auto cname = op.getClassName();

        {
            auto alloc_ft = FunctionType::get(ctx, {ptr}, {ptr});
            if (auto f = mod.lookupSymbol<func::FuncOp>("nv_alloc_object")) {
                if (f.getFunctionType() != alloc_ft) f.erase();
            }
            ensure_decl(mod, r, "nv_alloc_object", alloc_ft);
        }
        auto cname_ptr = str_ptr(mod, r, loc, cname);
        auto this_ptr  = func::CallOp::create(r, loc, TypeRange{ptr},
                                               "nv_alloc_object",
                                               ValueRange{cname_ptr})
                             .getResult(0);

        auto ctor_name = ("__ctor_" + cname).str();
        SmallVector<Type> ctor_params(1 + adaptor.getCtorArgs().size(), ptr);
        {
            auto ctor_ft = FunctionType::get(ctx, ctor_params, {});
            if (auto f = mod.lookupSymbol<func::FuncOp>(ctor_name)) {
                if (f.getFunctionType() != ctor_ft) f.erase();
            }
            ensure_decl(mod, r, ctor_name, ctor_ft);
        }
        SmallVector<Value> ctor_args;
        ctor_args.push_back(this_ptr);
        for (auto a : adaptor.getCtorArgs()) ctor_args.push_back(a);
        func::CallOp::create(r, loc, TypeRange{}, ctor_name, ctor_args);

        r.replaceOp(op, this_ptr);
        return success();
    }
};

} // namespace

void populateLowerNewOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc) {
    patterns.add<LowerNewOp>(tc, patterns.getContext());
}

} // namespace nv
