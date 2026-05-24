#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/new_expr_node.hpp"
#include "mlir/Dialect/Func/IR/FuncOps.h"

void NewExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto  loc      = ctx.loc(position.get());
    auto  vt       = ctx.get_narval_value_type();
    auto& b        = ctx.get_builder();
    auto& mlir_ctx = ctx.get_mlir_context();

    // Allocate the object as a map.
    mlir::Value obj = nir_call_runtime(ctx, loc, "nv_create_map", {}, {vt});

    // Store the class name as __class_name__ field (StringAttr key).
    auto cn_val = mlir::narval::ConstantOp::create(b, loc, vt,
        mlir::StringAttr::get(&mlir_ctx, class_name)).getResult();
    mlir::narval::SetFieldOp::create(b, loc, obj,
        mlir::StringAttr::get(&mlir_ctx, "__class_name__"), cn_val);

    // Build argument list: obj first, then each ctor arg.
    llvm::SmallVector<mlir::Value> ctor_args = {obj};
    for (const auto& arg : arguments) {
        if (arg) {
            arg->nir_codegen(ctx);
            if (ctx.has_value()) ctor_args.push_back(ctx.pop_value());
        }
    }

    // Call __ctor_ClassName if it exists in the module.
    const std::string& impl = base_class_name.empty() ? class_name : base_class_name;
    std::string ctor_name = "__ctor_" + impl;

    if (ctx.get_module().lookupSymbol(ctor_name)) {
        mlir::narval::CallOp::create(b, loc,
            mlir::TypeRange{},
            mlir::SymbolRefAttr::get(&mlir_ctx, ctor_name),
            ctor_args);
    }

    ctx.push_value(obj);
}
