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

    // ── Step 3: Method call — obj.method(args) → __method_<Class>_<method> ──
    // The receiver becomes the implicit first argument (self).
    if (caller && caller->kind == NodeType::MemberExpression) {
        auto* mem = static_cast<MemberExprNode*>(caller.get());
        if (mem->object && mem->property && mem->property->kind == NodeType::Identifier) {
            std::string method = static_cast<IdentifierNode*>(mem->property.get())->symbol;
            std::string owner  = ctx.find_method_owner(method);
            if (!owner.empty()) {
                mem->object->nir_codegen(ctx);
                llvm::SmallVector<mlir::Value> call_args;
                if (ctx.has_value()) call_args.push_back(ctx.pop_value());
                for (auto& v : arg_vals) call_args.push_back(v);

                std::string sym = "__method_" + owner + "_" + method;
                if (ctx.get_module().lookupSymbol(sym)) {
                    auto call = mlir::narval::CallOp::create(
                        ctx.get_builder(), loc,
                        mlir::TypeRange{vt},
                        mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), sym),
                        call_args);
                    ctx.push_value(call.getResults().empty() ? mlir::Value{}
                                                             : call.getResults()[0]);
                    return;
                }
                mlir::emitError(loc, "method '" + method + "' of class '" + owner +
                                     "' has no body to call");
                ctx.push_value(nir_emit_const(ctx, loc,
                    ctx.get_builder().getI64IntegerAttr(0)));
                return;
            }

            // vector.push(x) — no class owns the name, so this is the dynamic
            // list append; lower it to the bridge the vector literals use. The
            // expression still yields None so statements keep their stack shape.
            if (owner.empty() && method == "push" && arg_vals.size() == 1) {
                mem->object->nir_codegen(ctx);
                mlir::Value vec = ctx.pop_value();
                if (vec) nir_call_runtime(ctx, loc, "nv_vector_push", {vec, arg_vals[0]}, {});
                ctx.push_value(nir_call_runtime(ctx, loc, "nv_make_none", {}, {vt}));
                return;
            }
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
        {"len",    "nv_len_builtin"},
        {"json_field", "nv_json_field_builtin"},
        {"nv_sqlite_open", "nv_sqlite_open_builtin"},
        {"nv_sqlite_close", "nv_sqlite_close_builtin"},
        {"nv_sqlite_exec", "nv_sqlite_exec_builtin"},
        {"nv_sqlite_query", "nv_sqlite_query_builtin"},
        {"nv_sqlite_error", "nv_sqlite_error_builtin"},
        {"nv_sqlite_last_id", "nv_sqlite_last_id_builtin"},
        {"nv_sqlite_changes", "nv_sqlite_changes_builtin"},
        {"nv_sqlite_query_run", "nv_sqlite_query_run_builtin"},
        {"nv_sqlite_col_count", "nv_sqlite_col_count_builtin"},
        {"nv_sqlite_cell", "nv_sqlite_cell_builtin"},
        {"nv_file_open", "nv_file_open_builtin"},
        {"nv_file_close", "nv_file_close_builtin"},
        {"nv_file_read", "nv_file_read_builtin"},
        {"nv_file_read_line", "nv_file_read_line_builtin"},
        {"nv_file_write", "nv_file_write_builtin"},
        {"nv_file_exists", "nv_file_exists_builtin"},
        {"nv_file_remove", "nv_file_remove_builtin"},
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
        // `comptime import_c` prototypes: call the generic dlsym bridge for the
        // function's signature shape, passing the C symbol name as a boxed str.
        {
            const auto& sigs = ctx.get_c_import_sigs();
            auto sig = sigs.find(callee);
            if (sig != sigs.end()) {
                llvm::SmallVector<mlir::Value> rt_args;
                rt_args.push_back(mlir::narval::ConstantOp::create(
                    ctx.get_builder(), loc, vt,
                    mlir::StringAttr::get(&ctx.get_mlir_context(), callee)).getResult());
                for (auto& v : arg_vals) rt_args.push_back(v);
                std::string bridge = "nv_ffi_call_" + sig->second;
                ctx.push_value(nir_call_runtime(ctx, loc, bridge, rt_args, {vt}));
                return;
            }
        }

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

        // Local variable with a closure/function value — dispatch through the
        // closure handle at runtime. One nv_invoke_closure_N bridge per arity
        // (fixed module signatures; the runtime-call re-declaration cannot
        // serve variadic arities).
        if (ctx.lookup(callee)) {
            mlir::Value clo = ctx.lookup(callee);
            if (arg_vals.size() > 8) {
                mlir::emitError(loc, "closures with more than 8 arguments are "
                                     "not supported");
                ctx.push_value(nir_emit_const(ctx, loc,
                    ctx.get_builder().getI64IntegerAttr(0)));
                return;
            }
            llvm::SmallVector<mlir::Value> rt_args;
            rt_args.push_back(clo);
            for (auto& v : arg_vals) rt_args.push_back(v);
            std::string bridge =
                "nv_invoke_closure_" + std::to_string(arg_vals.size());
            ctx.push_value(nir_call_runtime(ctx, loc, bridge, rt_args, {vt}));
            return;
        }

        // Runtime / builtin call
        ctx.push_value(nir_call_runtime(ctx, loc, callee, arg_vals, {vt}));
        return;
    }

    // Unknown callee — return a zero placeholder
    ctx.push_value(nir_emit_const(ctx, loc, ctx.get_builder().getI64IntegerAttr(0)));
}
