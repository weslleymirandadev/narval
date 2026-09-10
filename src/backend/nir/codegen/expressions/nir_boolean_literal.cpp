#include "../nir_codegen_utils.hpp"
#include "backend/nir/NarvalOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "frontend/ast/expressions/boolean_literal_node.hpp"

void BooleanLiteralNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();
    auto& b  = ctx.get_builder();

    // nv_box_bool takes the C flag, so the argument has to be a raw i64
    // immediate: narval.constant builds a BOXED value (nv_box_int), which would
    // hand the runtime an object pointer as the flag and make every literal true.
    mlir::Value flag =
        mlir::arith::ConstantIntOp::create(b, loc, value ? 1 : 0, 64).getResult();

    ctx.ensure_runtime_func(
        "nv_box_bool", mlir::FunctionType::get(&ctx.get_mlir_context(), {b.getI64Type()}, {vt}));
    auto call = mlir::narval::CallRuntimeOp::create(
        b, loc, mlir::TypeRange{vt},
        mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), "nv_box_bool"),
        mlir::ValueRange{flag});
    ctx.push_value(call.getResults()[0]);
}
