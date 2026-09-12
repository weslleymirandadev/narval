#include "../nir_codegen_utils.hpp"
#include <cstdlib>
#include <string>
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include "frontend/ast/expressions/range_expr_node.hpp"

void DeclarationStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto  loc = ctx.loc(position.get());

    mlir::Value rhs;
    if (value) {
        // `mut v = 0..5` builds the vector [0, 1, 2, 3, 4]; a plain `v = 0..5` builds the array
        // {0, 1, 2, 3, 4}. Both bounds are integer literals for now; the range is unrolled here,
        // so nothing about it survives into the program.
        if (value->kind == NodeType::RangeExpression) {
            auto* range = static_cast<RangeExprNode*>(value.get());
            auto* lo = range->start && range->start->kind == NodeType::NumericLiteral
                     ? static_cast<NumericLiteralNode*>(range->start.get()) : nullptr;
            auto* hi = range->end && range->end->kind == NodeType::NumericLiteral
                     ? static_cast<NumericLiteralNode*>(range->end.get()) : nullptr;
            long long from = 0, to = 0;
            auto as_int = [](const std::string& text, long long& out) {
                if (text.empty() || text.find('.') != std::string::npos) return false;
                out = std::strtoll(text.c_str(), nullptr, 10);
                return true;
            };
            bool literal_bounds = lo && hi && as_int(lo->value, from) && as_int(hi->value, to);
            if (literal_bounds) {
                to += (range->inclusive ? 1 : 0);
                auto& b  = ctx.get_builder();
                auto  vt = ctx.get_narval_value_type();
                auto zero = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));
                if (mutable_) {
                    rhs = nir_call_runtime(ctx, loc, "nv_create_vector", {zero}, {vt});
                    for (long long v = from; v < to; ++v)
                        nir_call_runtime(ctx, loc, "nv_vector_push", {rhs,
                            nir_emit_const(ctx, loc, b.getI64IntegerAttr(v))}, {});
                } else {
                    auto sz = nir_emit_const(ctx, loc, b.getI64IntegerAttr(to > from ? to - from : 0));
                    rhs = nir_call_runtime(ctx, loc, "nv_create_array", {sz}, {vt});
                    long long idx = 0;
                    for (long long v = from; v < to; ++v, ++idx)
                        nir_call_runtime(ctx, loc, "nv_array_set", {rhs,
                            nir_emit_const(ctx, loc, b.getI64IntegerAttr(idx)),
                            nir_emit_const(ctx, loc, b.getI64IntegerAttr(v))}, {});
                }
            }
        }
        if (!rhs) {
            value->nir_codegen(ctx);
            rhs = ctx.pop_value();
        }
    }
    if (!rhs)
        rhs = nir_emit_const(ctx, loc, ctx.get_builder().getI64IntegerAttr(0));

    std::string binding;
    if (target && target->kind == NodeType::Identifier)
        binding = static_cast<IdentifierNode*>(target.get())->symbol;

    if (!binding.empty()) {
        ctx.define(binding, rhs);
        if (ctx.is_repl_global(binding)) {
            // In the REPL this declaration is the line that gave the variable its
            // value; the following inputs get a fresh JIT, so keep it in the runtime
            // store (nv_repl_get reads it back there).
            auto& b  = ctx.get_builder();
            auto  vt = ctx.get_narval_value_type();
            ctx.ensure_runtime_func("nv_repl_set",
                mlir::FunctionType::get(&ctx.get_mlir_context(), {vt, vt}, {vt}));
            mlir::Value name_val = nir_emit_const(ctx, loc, b.getStringAttr(binding));
            mlir::narval::CallRuntimeOp::create(
                b, loc, mlir::TypeRange{vt},
                mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), "nv_repl_set"),
                mlir::ValueRange{name_val, rhs});
        }
    }
}
