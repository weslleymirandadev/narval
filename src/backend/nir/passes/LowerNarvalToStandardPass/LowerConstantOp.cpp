#include "backend/nir/NarvalOps.h"
#include "backend/nir/passes/NarvalTypeConverter.hpp"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

//===----------------------------------------------------------------------===//
// narval.constant → nv_box_int / nv_box_float / nv_box_str
//===----------------------------------------------------------------------===//

struct LowerConstantOp : public OpConversionPattern<ConstantOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(ConstantOp op, OpAdaptor,
                                  ConversionPatternRewriter& r) const override {
        auto loc = op.getLoc();
        auto* ctx = r.getContext();
        auto ptr = LLVM::LLVMPointerType::get(ctx);
        auto mod = op->getParentOfType<ModuleOp>();

        if (auto ia = mlir::dyn_cast<IntegerAttr>(op.getValue())) {
            auto i64 = r.getI64Type();
            auto ft  = FunctionType::get(ctx, {i64}, {ptr});
            if (auto f = mod.lookupSymbol<func::FuncOp>("nv_box_int")) {
                if (f.getFunctionType() != ft) f.erase();
            }
            ensure_decl(mod, r, "nv_box_int", ft);
            auto raw = arith::ConstantOp::create(r, loc,
                IntegerAttr::get(i64, ia.getInt()));
            r.replaceOp(op, func::CallOp::create(r, loc, TypeRange{ptr},
                                                  "nv_box_int",
                                                  ValueRange{raw.getResult()})
                               .getResult(0));
            return success();
        }
        if (auto fa = mlir::dyn_cast<FloatAttr>(op.getValue())) {
            auto f64 = r.getF64Type();
            auto ft  = FunctionType::get(ctx, {f64}, {ptr});
            if (auto f = mod.lookupSymbol<func::FuncOp>("nv_box_float")) {
                if (f.getFunctionType() != ft) f.erase();
            }
            ensure_decl(mod, r, "nv_box_float", ft);
            auto raw = arith::ConstantOp::create(r, loc,
                FloatAttr::get(f64, fa.getValueAsDouble()));
            r.replaceOp(op, func::CallOp::create(r, loc, TypeRange{ptr},
                                                  "nv_box_float",
                                                  ValueRange{raw.getResult()})
                               .getResult(0));
            return success();
        }
        if (auto sa = mlir::dyn_cast<StringAttr>(op.getValue())) {
            auto ft  = FunctionType::get(ctx, {ptr}, {ptr});
            if (auto f = mod.lookupSymbol<func::FuncOp>("nv_box_str")) {
                if (f.getFunctionType() != ft) f.erase();
            }
            ensure_decl(mod, r, "nv_box_str", ft);
            auto s = str_ptr(mod, r, loc, sa.getValue());
            r.replaceOp(op, func::CallOp::create(r, loc, TypeRange{ptr},
                                                  "nv_box_str", ValueRange{s})
                               .getResult(0));
            return success();
        }
        return failure();
    }
};

} // namespace

void populateLowerConstantOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc) {
    patterns.add<LowerConstantOp>(tc, patterns.getContext());
}

} // namespace nv
