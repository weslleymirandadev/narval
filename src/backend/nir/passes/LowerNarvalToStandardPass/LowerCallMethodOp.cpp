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
// narval.call_method → __method_ClassName_name
//===----------------------------------------------------------------------===//

struct LowerCallMethodOp : public OpConversionPattern<CallMethodOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(CallMethodOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        auto loc   = op.getLoc();
        auto* ctx  = r.getContext();
        auto ptr   = LLVM::LLVMPointerType::get(ctx);
        auto mod   = op->getParentOfType<ModuleOp>();
        auto mname = ("__method_" + op.getClassName() + "_"
                      + op.getMethodName()).str();
        SmallVector<Type> params(1 + adaptor.getArgs().size(), ptr);
        {
            auto cm_ft = FunctionType::get(ctx, params, {ptr});
            if (auto f = mod.lookupSymbol<func::FuncOp>(mname)) {
                if (f.getFunctionType() != cm_ft) f.erase();
            }
            ensure_decl(mod, r, mname, cm_ft);
        }
        SmallVector<Value> args;
        args.push_back(adaptor.getReceiver());
        for (auto a : adaptor.getArgs()) args.push_back(a);
        r.replaceOp(op,
            func::CallOp::create(r, loc, TypeRange{ptr}, mname, args)
                .getResult(0));
        return success();
    }
};

} // namespace

void populateLowerCallMethodOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc) {
    patterns.add<LowerCallMethodOp>(tc, patterns.getContext());
}

} // namespace nv
