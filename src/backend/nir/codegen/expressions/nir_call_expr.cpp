#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/call_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"
#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include "mlir/Dialect/Func/IR/FuncOps.h"

void CallExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();

    // ── Tensor method calls: tensor.item(), tensor.tolist() ──────────
    // Recognise MemberExprNode(obj, "item"/"tolist") and emit runtime call.
    if (caller && caller->kind == NodeType::MemberExpression) {
        auto* member = static_cast<MemberExprNode*>(caller.get());
        if (member->property && member->property->kind == NodeType::Identifier) {
            auto* prop = static_cast<IdentifierNode*>(member->property.get());
            if (prop->symbol == "item") {
                // Evaluate the object (the tensor)
                if (member->object) member->object->nir_codegen(ctx);
                mlir::Value obj = ctx.has_value() ? ctx.pop_value() : mlir::Value();
                if (!obj) { ctx.push_value({}); return; }
                auto result = nir_call_runtime(ctx, loc, "nv_tensor_item_bridge", {obj}, {vt});
                ctx.push_value(result);
                return;
            }
            if (prop->symbol == "tolist") {
                if (member->object) member->object->nir_codegen(ctx);
                mlir::Value obj = ctx.has_value() ? ctx.pop_value() : mlir::Value();
                if (!obj) { ctx.push_value({}); return; }
                auto result = nir_call_runtime(ctx, loc, "nv_tensor_tolist_bridge", {obj}, {vt});
                ctx.push_value(result);
                return;
            }
        }
    }

    // ── Tensor.zeros(d0, d1, ...) / Tensor.ones(d0, d1, ...) ──────────
    // Recognise MemberExprNode(Tensor, zeros/ones) pattern and emit
    // narval.tensor_fill with the correct shape attribute.
    if (caller && caller->kind == NodeType::MemberExpression) {
        auto* member = static_cast<MemberExprNode*>(caller.get());
        if (member->object && member->object->kind == NodeType::Identifier &&
            member->property && member->property->kind == NodeType::Identifier) {
            auto* obj  = static_cast<IdentifierNode*>(member->object.get());
            auto* prop = static_cast<IdentifierNode*>(member->property.get());
            if (obj->symbol == "Tensor" &&
                (prop->symbol == "zeros" || prop->symbol == "ones")) {
                // Extract dimension values from args
                llvm::SmallVector<int64_t> dims;
                for (const auto& arg : args) {
                    if (arg && arg->value && arg->value->kind == NodeType::NumericLiteral) {
                        auto* num = static_cast<NumericLiteralNode*>(arg->value.get());
                        if (num->value.find('.') == std::string::npos) {
                            dims.push_back(std::stoll(num->value));
                            continue;
                        }
                    }
                    // Fallback: emit const 0 placeholder
                    dims.push_back(0);
                }
                // Fill value: 0 for zeros, 1 for ones
                int64_t fill_val = (prop->symbol == "zeros") ? 0 : 1;
                auto& b = ctx.get_builder();
                
                // Use nv_tensor_fill_nd(fill, ndim, d0..d7) — fixed args, no varargs.
                llvm::SmallVector<mlir::Type> arg_types(10, b.getIntegerType(64));
                llvm::SmallVector<mlir::Value> rt_args;
                
                auto fill_attr = b.getI64IntegerAttr(fill_val);
                rt_args.push_back(b.create<mlir::arith::ConstantOp>(loc, fill_attr).getResult());
                auto ndim_attr = b.getI64IntegerAttr((int64_t)dims.size());
                rt_args.push_back(b.create<mlir::arith::ConstantOp>(loc, ndim_attr).getResult());
                for (int j = 0; j < 8; j++) {
                    int64_t d = (j < (int)dims.size()) ? dims[j] : 0;
                    auto d_attr = b.getI64IntegerAttr(d);
                    rt_args.push_back(b.create<mlir::arith::ConstantOp>(loc, d_attr).getResult());
                }
                
                auto fn_type = mlir::FunctionType::get(&ctx.get_mlir_context(), arg_types, vt);
                ctx.ensure_runtime_func("nv_tensor_fill_nd", fn_type);
                auto call = mlir::narval::CallRuntimeOp::create(
                    b, loc, mlir::TypeRange{vt},
                    mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), "nv_tensor_fill_nd"),
                    rt_args);
                ctx.push_value(call.getResults()[0]);
                return;
            }
        }
    }

    // ── Tensor(d0, d1, ...) — constructor call ───────────────────────
    if (caller && caller->kind == NodeType::Identifier) {
        auto* id = static_cast<IdentifierNode*>(caller.get());
        if (id->symbol == "Tensor") {
            llvm::SmallVector<int64_t> dims;
            for (const auto& arg : args) {
                if (arg && arg->value && arg->value->kind == NodeType::NumericLiteral) {
                    auto* num = static_cast<NumericLiteralNode*>(arg->value.get());
                    if (num->value.find('.') == std::string::npos) {
                        dims.push_back(std::stoll(num->value));
                        continue;
                    }
                }
                dims.push_back(0);
            }
            auto zero = mlir::narval::ConstantOp::create(
                ctx.get_builder(), loc, vt,
                ctx.get_builder().getI64IntegerAttr(0)).getResult();
            auto& b = ctx.get_builder();
            llvm::SmallVector<mlir::Type> arg_types(10, b.getIntegerType(64));
            llvm::SmallVector<mlir::Value> rt_args;
            auto fill_attr = b.getI64IntegerAttr(0);
            rt_args.push_back(b.create<mlir::arith::ConstantOp>(loc, fill_attr).getResult());
            auto ndim_attr = b.getI64IntegerAttr((int64_t)dims.size());
            rt_args.push_back(b.create<mlir::arith::ConstantOp>(loc, ndim_attr).getResult());
            for (int j = 0; j < 8; j++) {
                int64_t d = (j < (int)dims.size()) ? dims[j] : 0;
                auto d_attr = b.getI64IntegerAttr(d);
                rt_args.push_back(b.create<mlir::arith::ConstantOp>(loc, d_attr).getResult());
            }
            auto fn_type = mlir::FunctionType::get(&ctx.get_mlir_context(), arg_types, vt);
            ctx.ensure_runtime_func("nv_tensor_fill_nd", fn_type);
            auto call = mlir::narval::CallRuntimeOp::create(
                b, loc, mlir::TypeRange{vt},
                mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), "nv_tensor_fill_nd"),
                rt_args);
            ctx.push_value(call.getResults()[0]);
            return;
        }
    }

    // Collect argument values.
    llvm::SmallVector<mlir::Value> arg_vals;
    for (const auto& arg : args) {
        if (arg && arg->value) {
            arg->value->nir_codegen(ctx);
            if (ctx.has_value()) arg_vals.push_back(ctx.pop_value());
        }
    }

    // Resolve callee name.
    std::string callee;
    if (caller && caller->kind == NodeType::Identifier)
        callee = static_cast<IdentifierNode*>(caller.get())->symbol;

    // Remap Narval builtins whose C names conflict with keywords or nv_ convention.
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
        // FFI C registry remap: `from extern "C:lib" import fn` maps user name
        // to the nv_ffi_* bridge to avoid shadowing C library symbols.
        {
            auto remapped = ctx.get_ffi_remap(callee);
            if (!remapped.empty())
                callee = remapped;
        }

        // User-defined function already in the module.
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

        // Runtime / builtin call.
        ctx.push_value(nir_call_runtime(ctx, loc, callee, arg_vals, {vt}));
        return;
    }

    // Unknown callee — return a zero placeholder.
    ctx.push_value(nir_emit_const(ctx, loc, ctx.get_builder().getI64IntegerAttr(0)));
}
