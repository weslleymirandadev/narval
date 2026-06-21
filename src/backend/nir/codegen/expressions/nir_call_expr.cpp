#include "../nir_codegen_utils.hpp"
#include "backend/nir/nir_tensor_codegen.hpp"
#include "frontend/ast/expressions/call_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"
#include "mlir/Dialect/Func/IR/FuncOps.h"

void CallExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();

    // ── Step 1: Tensor dispatch ─────────────────────────────────────────
    if (nv_tensor_codegen::try_handle_call(ctx, static_cast<Node*>(this)))
        return;

    // ── Step 2: Collect argument values ─────────────────────────────────
    llvm::SmallVector<mlir::Value> arg_vals;
    for (const auto& arg : args) {
        if (arg && arg->value) {
            arg->value->nir_codegen(ctx);
            if (ctx.has_value()) arg_vals.push_back(ctx.pop_value());
        }
    }

    // Resolve callee name
    std::string callee;
    if (caller && caller->kind == NodeType::Identifier)
        callee = static_cast<IdentifierNode*>(caller.get())->symbol;

    // Remap Narval builtins whose C names conflict with keywords
    static const std::pair<const char*, const char*> kBuiltinRemap[] = {
        {"write",  "nv_write_bridge"},
        {"int",    "nv_int_builtin"},
        {"float",  "nv_float_builtin"},
        {"bool",   "nv_bool_builtin"},
        {"char",   "nv_char_builtin"},
        {"str",    "nv_str_builtin"},
        {"exit",   "nv_exit_builtin"},
        {nullptr, nullptr}
    };
    if (!callee.empty()) {
        for (int i = 0; kBuiltinRemap[i].first; ++i) {
            if (callee == kBuiltinRemap[i].first) {
                callee = kBuiltinRemap[i].second;
                break;
            }
        }
    }

    if (!callee.empty()) {
        // FFI C registry remap
        {
            auto remapped = ctx.get_ffi_remap(callee);
            if (!remapped.empty())
                callee = remapped;
        }

        // User-defined function already in the module
        if (ctx.get_module().lookupSymbol(callee)) {
            llvm::SmallVector<mlir::Type> arg_types(arg_vals.size(), vt);
            auto call = mlir::narval::CallOp::create(
                ctx.get_builder(), loc,
                mlir::TypeRange{vt},
                mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), callee),
                arg_vals);
            ctx.push_value(call.getResults().empty() ? mlir::Value{} : call.getResults()[0]);
            return;
        }

        // Runtime / builtin call
        ctx.push_value(nir_call_runtime(ctx, loc, callee, arg_vals, {vt}));
        return;
    }

    // Unknown callee — return a zero placeholder
    ctx.push_value(nir_emit_const(ctx, loc, ctx.get_builder().getI64IntegerAttr(0)));
}
