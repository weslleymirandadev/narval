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

    // `read([prompt])` is the only builtin with an optional argument, and its
    // bridge always takes the prompt. With no argument emitted, the callee read a
    // stale register (the previous call's first argument) as the prompt — so
    // `read()` printed the string the last `write` had passed. Pass an explicit
    // None when the prompt is omitted.
    if (arg_vals.empty() && caller && caller->kind == NodeType::Identifier &&
        static_cast<IdentifierNode*>(caller.get())->symbol == "read") {
        ctx.push_value(nir_call_runtime(ctx, loc, "nv_make_none", {}, {vt}));
        arg_vals.push_back(ctx.pop_value());
    }

    // ── Step 3a: namespace member call — `ns.f(a)` where `ns` is an imported module
    // namespace (`import m as ns;` / `from "x" import * as ns`). It is a call to the
    // module's own function `f`; the alias itself never becomes a value (it used to
    // read as unbound and print 0).
    std::string callee;
    if (caller && caller->kind == NodeType::MemberExpression) {
        auto* mem = static_cast<MemberExprNode*>(caller.get());
        if (mem->object && mem->object->kind == NodeType::Identifier &&
            mem->property && mem->property->kind == NodeType::Identifier &&
            ctx.is_namespace_alias(static_cast<IdentifierNode*>(mem->object.get())->symbol)) {
            callee = static_cast<IdentifierNode*>(mem->property.get())->symbol;
        }
    }

    // ── Step 3b: the `json` global — `json.parse(text)`, `json.parseString(text)`,
    // `json.stringify(value)`, `json.dump(value, path)`. The checker declares the name
    // and its four methods, but nothing was bound to `json` at run time, so every call
    // warned "no value bound for 'json'" and evaluated to 0.
    if (callee.empty() && caller && caller->kind == NodeType::MemberExpression) {
        auto* mem = static_cast<MemberExprNode*>(caller.get());
        if (mem->object && mem->object->kind == NodeType::Identifier &&
            mem->property && mem->property->kind == NodeType::Identifier &&
            static_cast<IdentifierNode*>(mem->object.get())->symbol == "json") {
            const std::string& m = static_cast<IdentifierNode*>(mem->property.get())->symbol;
            if (m == "parse" || m == "parseString")  callee = "nv_json_parse";
            else if (m == "stringify")               callee = "nv_json_stringify";
            else if (m == "dump")                    callee = "nv_json_dump";
        }
    }

    // ── Step 3c: interface-typed receiver — dispatch by name at run time ──────
    // An interface proves the method exists, not which class provides it: any class that
    // says `implements I` may be behind the value (subclasses included), so the call
    // cannot be resolved to __method_<Class>_<name> at compile time. The bridge reads the
    // receiver's __class_name__ and dlsym()s the method symbol — the link exports
    // __method_* the same way it exports the closure functions.
    if (callee.empty() && caller && caller->kind == NodeType::MemberExpression) {
        auto* mem = static_cast<MemberExprNode*>(caller.get());
        if (mem->object && mem->property && mem->property->kind == NodeType::Identifier &&
            !mem->interface_dispatch.empty()) {
            const std::string method =
                static_cast<IdentifierNode*>(mem->property.get())->symbol;
            if (arg_vals.size() > 8) {
                mlir::emitError(loc, "interface dispatch of '" + method +
                                     "' with more than 8 arguments is not supported");
                ctx.push_value(nir_emit_const(ctx, loc,
                    ctx.get_builder().getI64IntegerAttr(0)));
                return;
            }

            mem->object->nir_codegen(ctx);
            llvm::SmallVector<mlir::Value> rt_args;
            if (ctx.has_value()) rt_args.push_back(ctx.pop_value());
            rt_args.push_back(mlir::narval::ConstantOp::create(
                ctx.get_builder(), loc, vt,
                mlir::StringAttr::get(&ctx.get_mlir_context(), method)).getResult());
            for (auto& v : arg_vals) rt_args.push_back(v);

            std::string bridge =
                "nv_dispatch_method_" + std::to_string(arg_vals.size());
            mlir::Value dispatched = nir_call_runtime(ctx, loc, bridge, rt_args, {vt});

            // A method typed None returns nothing, so there is no result to keep: the
            // value in the register is whatever the callee left there.
            ctx.push_value(mem->interface_void_result
                ? nir_call_runtime(ctx, loc, "nv_make_none", {}, {vt})
                : dispatched);
            return;
        }
    }

    // ── Step 3: Method call — obj.method(args) → __method_<Class>_<method> ──
    // The receiver becomes the implicit first argument (self).
    if (callee.empty() && caller && caller->kind == NodeType::MemberExpression) {
        auto* mem = static_cast<MemberExprNode*>(caller.get());
        if (mem->object && mem->property && mem->property->kind == NodeType::Identifier) {
            std::string method = static_cast<IdentifierNode*>(mem->property.get())->symbol;
            // The checker resolved the owner from the receiver's static type; the
            // name-only lookup stays as a fallback for calls that did not go through it
            // (it cannot tell two classes with the same method apart).
            std::string owner  = mem->resolved_owner.empty()
                               ? ctx.find_method_owner(method)
                               : mem->resolved_owner;
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

            // vector.append(x) / vector.push(x) — no class owns either name, so this is
            // the dynamic list append; lower it to the bridge the vector literals use. The
            // expression still yields None so statements keep their stack shape.
            if (owner.empty() && (method == "push" || method == "append") &&
                arg_vals.size() == 1) {
                mem->object->nir_codegen(ctx);
                mlir::Value vec = ctx.pop_value();
                if (vec) nir_call_runtime(ctx, loc, "nv_vector_push", {vec, arg_vals[0]}, {});
                ctx.push_value(nir_call_runtime(ctx, loc, "nv_make_none", {}, {vt}));
                return;
            }
        }
    }

    // Resolve callee name (a namespace member call already resolved it above)
    if (callee.empty() && caller && caller->kind == NodeType::Identifier) {
        callee = static_cast<IdentifierNode*>(caller.get())->symbol;
        // `from "m" import square as sq; sq(6)` — the merged module defines `square`,
        // so the alias has to be translated or the link fails with "undefined
        // reference to `sq'".
        callee = ctx.resolve_symbol_alias(callee);
    }

    // Remap Narval builtins whose C names conflict with keywords
    static const std::pair<const char*, const char*> kBuiltinRemap[] = {
        {"write",  "nv_write_bridge"},
        {"par_for", "nv_parallel_for"},
        {"spawn",  "nv_thread_spawn"},
        {"wait",   "nv_thread_join"},
        {"chan",   "nv_channel_new"},
        {"send",   "nv_channel_send"},
        {"recv",   "nv_channel_recv"},
        {"read",   "nv_read_builtin"},
        {"int",    "nv_int_builtin"},
        {"int64",  "nv_int_builtin"},   // the width is a type-level discipline for now
        {"int32",  "nv_int_builtin"},
        {"float",  "nv_float_builtin"},
        {"float64","nv_float_builtin"},
        {"float32","nv_float_builtin"},
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
        {"nv_sqlite_col_name", "nv_sqlite_col_name_builtin"},
        {"nv_file_open", "nv_file_open_builtin"},
        {"nv_file_close", "nv_file_close_builtin"},
        {"nv_file_read", "nv_file_read_builtin"},
        {"nv_file_read_line", "nv_file_read_line_builtin"},
        {"nv_file_write", "nv_file_write_builtin"},
        {"nv_file_exists", "nv_file_exists_builtin"},
        {"nv_file_remove", "nv_file_remove_builtin"},
        {"nv_net_tcp_listen", "nv_net_tcp_listen_builtin"},
        {"nv_net_tcp_connect", "nv_net_tcp_connect_builtin"},
        {"nv_net_accept", "nv_net_accept_builtin"},
        {"nv_net_udp_bind", "nv_net_udp_bind_builtin"},
        {"nv_net_udp_send_to", "nv_net_udp_send_to_builtin"},
        {"nv_net_udp_recv_from", "nv_net_udp_recv_from_builtin"},
        {"nv_net_send", "nv_net_send_builtin"},
        {"nv_net_recv", "nv_net_recv_builtin"},
        {"nv_net_recv_until", "nv_net_recv_until_builtin"},
        {"nv_net_recv_exact", "nv_net_recv_exact_builtin"},
        {"nv_net_shutdown", "nv_net_shutdown_builtin"},
        {"nv_net_close", "nv_net_close_builtin"},
        {"nv_net_set_timeout", "nv_net_set_timeout_builtin"},
        {"nv_net_set_option", "nv_net_set_option_builtin"},
        {"nv_net_get_option", "nv_net_get_option_builtin"},
        {"nv_net_status", "nv_net_status_builtin"},
        {"nv_net_last_error", "nv_net_last_error_builtin"},
        {"nv_net_last_len", "nv_net_last_len_builtin"},
        {"nv_net_addr_parse", "nv_net_addr_parse_builtin"},
        {"nv_net_resolve", "nv_net_resolve_builtin"},
        {"nv_net_resolve_all", "nv_net_resolve_all_builtin"},
        {"nv_net_hostname", "nv_net_hostname_builtin"},
        {"nv_net_host_of", "nv_net_host_of_builtin"},
        {"nv_net_port_of", "nv_net_port_of_builtin"},
        {"nv_net_local_port", "nv_net_local_port_builtin"},
        {"nv_net_local_addr", "nv_net_local_addr_builtin"},
        {"nv_net_peer_addr", "nv_net_peer_addr_builtin"},
        {"nv_net_poll", "nv_net_poll_builtin"},
        // Option/Result constructors: the checker accepts them as `Some(x)` etc, but the
        // codegen emitted the bare name as the callee, which is a symbol nothing defines
        // — every compiled program using them failed to link. The runtime builds the
        // objects (they are ordinary values).
        {"Some", "nv_make_some"},
        {"Ok",   "nv_make_ok"},
        {"Err",  "nv_make_err"},
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
