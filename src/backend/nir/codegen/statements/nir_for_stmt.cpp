#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/for_stmt_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"

static std::string get_binding_name(const ForStmtNode& node) {
    if (!node.bindings.empty())
        if (node.bindings[0] && node.bindings[0]->kind == NodeType::Identifier)
            return static_cast<IdentifierNode*>(node.bindings[0].get())->symbol;
    return "";
}

void ForStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());
    auto  vt  = ctx.get_narval_value_type();
    auto  idx = b.getIndexType();
    auto  i32 = b.getI32Type();

    std::string var = get_binding_name(*this);

    // ── Range-based: for x in lb..ub ──────────────────────────────────────
    if (range_start && range_end) {
        range_start->nir_codegen(ctx);
        mlir::Value lb_v = ctx.pop_value();
        range_end->nir_codegen(ctx);
        mlir::Value ub_v = ctx.pop_value();

        auto to_idx_fn = mlir::FunctionType::get(&ctx.get_mlir_context(), {vt}, {idx});
        ctx.ensure_runtime_func("nv_value_to_index", to_idx_fn);
        auto to_idx = ctx.get_module().lookupSymbol<mlir::func::FuncOp>("nv_value_to_index");

        auto lb = mlir::func::CallOp::create(b, loc, to_idx, {lb_v}).getResult(0);
        auto ub = mlir::func::CallOp::create(b, loc, to_idx, {ub_v}).getResult(0);
        if (range_inclusive) {
            auto one = mlir::arith::ConstantIndexOp::create(b, loc, 1).getResult();
            ub = mlir::arith::AddIOp::create(b, loc, ub, one).getResult();
        }
        auto step = mlir::arith::ConstantIndexOp::create(b, loc, 1).getResult();

        auto for_op = ctx.emit_for_range(loc, lb, ub, step, var);
        {
            mlir::OpBuilder::InsertionGuard g(b);
            auto* body_blk = &for_op.getBody().front();
            b.setInsertionPointToStart(body_blk);
            ctx.push_scope();

            // Box index → narval.value for use in body.
            mlir::Value iv = body_blk->getArgument(0);
            auto box_fn_t = mlir::FunctionType::get(&ctx.get_mlir_context(), {idx}, {vt});
            ctx.ensure_runtime_func("nv_index_to_value", box_fn_t);
            auto box_fn = ctx.get_module().lookupSymbol<mlir::func::FuncOp>("nv_index_to_value");
            auto iv_boxed = mlir::func::CallOp::create(b, loc, box_fn, {iv}).getResult(0);
            if (!var.empty()) ctx.define(var, iv_boxed);

            nir_emit_body(body, ctx);
            ctx.pop_scope();
            nir_terminate(*body_blk, b, loc);
        }
        b.setInsertionPointAfter(for_op);
        return;
    }

    // ── Collection-based: for x in iterable ───────────────────────────────
    if (iterable) {
        iterable->nir_codegen(ctx);
        mlir::Value iter = ctx.pop_value();

        auto len_fn_t = mlir::FunctionType::get(&ctx.get_mlir_context(), {vt}, {i32});
        ctx.ensure_runtime_func("nv_len", len_fn_t);
        auto len_fn = ctx.get_module().lookupSymbol<mlir::func::FuncOp>("nv_len");
        auto len_i32 = mlir::func::CallOp::create(b, loc, len_fn, {iter}).getResult(0);
        auto len_idx = mlir::arith::IndexCastOp::create(b, loc, idx, len_i32).getResult();

        auto zero = mlir::arith::ConstantIndexOp::create(b, loc, 0).getResult();
        auto one  = mlir::arith::ConstantIndexOp::create(b, loc, 1).getResult();

        auto for_op = ctx.emit_for_range(loc, zero, len_idx, one, var);
        {
            mlir::OpBuilder::InsertionGuard g(b);
            auto* body_blk = &for_op.getBody().front();
            b.setInsertionPointToStart(body_blk);
            ctx.push_scope();

            mlir::Value iv    = body_blk->getArgument(0);
            auto iv_i32 = mlir::arith::IndexCastOp::create(b, loc, i32, iv).getResult();

            auto get_fn_t = mlir::FunctionType::get(&ctx.get_mlir_context(), {vt, i32}, {vt});
            ctx.ensure_runtime_func("nv_get_at", get_fn_t);
            auto get_fn = ctx.get_module().lookupSymbol<mlir::func::FuncOp>("nv_get_at");
            auto elem = mlir::func::CallOp::create(b, loc, get_fn, {iter, iv_i32}).getResult(0);
            if (!var.empty()) ctx.define(var, elem);

            nir_emit_body(body, ctx);
            ctx.pop_scope();
            nir_terminate(*body_blk, b, loc);
        }
        b.setInsertionPointAfter(for_op);
    }
}
