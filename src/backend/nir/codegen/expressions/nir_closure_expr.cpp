#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/closure_expr_node.hpp"
#include "mlir/Dialect/Func/IR/FuncOps.h"

static int g_closure_counter = 0;

void ClosureExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto  loc = ctx.loc(position.get());
    auto  vt  = ctx.get_narval_value_type();
    auto& b   = ctx.get_builder();
    auto& mlir_ctx = ctx.get_mlir_context();

    mlir::OpBuilder::InsertionGuard guard(b);

    // Generate a unique name for this closure function.
    std::string fn_name = "__closure_fn_" + std::to_string(g_closure_counter++);

    // All params are narval.value; captures are appended after declared params.
    std::vector<mlir::Type> param_types(parameters.size(), vt);
    for (size_t i = 0; i < captures.size(); ++i)
        param_types.push_back(vt);

    bool is_void = (return_type == "None" || return_type == "void" || return_type.empty());
    mlir::TypeRange ret_types = is_void ? mlir::TypeRange{} : mlir::TypeRange{vt};

    auto fn_type = mlir::FunctionType::get(&mlir_ctx, param_types, ret_types);
    auto fn = mlir::func::FuncOp::create(b, loc, fn_name, fn_type);
    fn.setPrivate();

    auto* entry = fn.addEntryBlock();
    b.setInsertionPointToStart(entry);

    ctx.push_scope();

    // Bind declared parameters.
    for (size_t i = 0; i < parameters.size(); ++i)
        ctx.define(parameters[i].first, fn.getArgument(i));

    // Bind captured variables.
    for (size_t i = 0; i < captures.size(); ++i)
        ctx.define(captures[i], fn.getArgument(parameters.size() + i));

    nir_emit_body(body, ctx);

    auto* last_bb = &fn.getBody().back();
    if (last_bb->empty() || !last_bb->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
        if (is_void) {
            mlir::func::ReturnOp::create(b, loc);
        } else {
            mlir::Value zero = ctx.has_value()
                ? ctx.pop_value()
                : mlir::narval::ConstantOp::create(b, loc, vt, b.getI64IntegerAttr(0)).getResult();
            mlir::func::ReturnOp::create(b, loc, mlir::ValueRange{zero});
        }
    }

    ctx.pop_scope();
    ctx.get_module().push_back(fn);

    // Collect current-scope values for captured names.
    llvm::SmallVector<mlir::Value> captured_vals;
    for (const auto& cap : captures) {
        mlir::Value v = ctx.lookup(cap);
        if (!v) v = mlir::narval::ConstantOp::create(b, loc, vt, b.getI64IntegerAttr(0)).getResult();
        captured_vals.push_back(v);
    }

    // create_closure(fn_sym, cap0, cap1, ...) -> Value
    // Use narval.call_runtime with all capture values appended.
    llvm::SmallVector<mlir::Value> rt_args;
    // The closure wrapper needs a reference to fn — use a string constant as a symbol proxy.
    auto fn_name_val = mlir::narval::ConstantOp::create(b, loc, vt,
        mlir::StringAttr::get(&mlir_ctx, fn_name)).getResult();
    rt_args.push_back(fn_name_val);
    for (auto& v : captured_vals) rt_args.push_back(v);

    ctx.push_value(nir_call_runtime(ctx, loc, "nv_create_closure", rt_args, {vt}));
}
