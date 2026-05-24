#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/list_comp_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"

// [elt for var in src if cond] — lowers to narval.for_range over the source length.
void ListCompNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto  loc      = ctx.loc(position.get());
    auto  vt       = ctx.get_narval_value_type();
    auto& b        = ctx.get_builder();
    auto& mlir_ctx = ctx.get_mlir_context();

    auto sz_zero = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));
    mlir::Value out = nir_call_runtime(ctx, loc, "nv_create_vector", {sz_zero}, {vt});

    if (generators.empty()) { ctx.push_value(out); return; }

    auto& gen = generators[0];
    gen.second->nir_codegen(ctx);
    mlir::Value src = ctx.has_value() ? ctx.pop_value() : sz_zero;
    if (!src) src = sz_zero;

    // Get length as i32 then cast to index.
    auto i32 = b.getI32Type();
    auto idx = b.getIndexType();

    auto len_fn_t = mlir::FunctionType::get(&mlir_ctx, {vt}, {i32});
    ctx.ensure_runtime_func("nv_len", len_fn_t);
    auto len_fn = ctx.get_module().lookupSymbol<mlir::func::FuncOp>("nv_len");
    auto len_i32 = mlir::func::CallOp::create(b, loc, len_fn, {src}).getResult(0);
    auto len_idx = mlir::arith::IndexCastOp::create(b, loc, idx, len_i32).getResult();

    auto zero_idx = mlir::arith::ConstantIndexOp::create(b, loc, 0).getResult();
    auto one_idx  = mlir::arith::ConstantIndexOp::create(b, loc, 1).getResult();

    auto for_op = ctx.emit_for_range(loc, zero_idx, len_idx, one_idx, "__lc_i__");

    {
        mlir::OpBuilder::InsertionGuard g(b);
        auto* body_blk = &for_op.getBody().front();
        b.setInsertionPointToStart(body_blk);

        // Loop induction var is index — cast to i32 for runtime indexing.
        auto iv     = body_blk->getArgument(0);
        auto iv_i32 = mlir::arith::IndexCastOp::create(b, loc, i32, iv).getResult();

        // Get element from source.
        auto get_fn_t = mlir::FunctionType::get(&mlir_ctx, {vt, i32}, {vt});
        ctx.ensure_runtime_func("nv_get_at", get_fn_t);
        auto get_fn = ctx.get_module().lookupSymbol<mlir::func::FuncOp>("nv_get_at");
        auto elem = mlir::func::CallOp::create(b, loc, get_fn, {src, iv_i32}).getResult(0);

        ctx.push_scope();

        // Bind loop variable.
        if (gen.first && gen.first->kind == NodeType::Identifier) {
            auto vname = static_cast<IdentifierNode*>(gen.first.get())->symbol;
            ctx.define(vname, elem);
        }

        if (if_cond) {
            if_cond->nir_codegen(ctx);
            mlir::Value cond = ctx.has_value() ? ctx.pop_value() : mlir::Value{};
            if (!cond) cond = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));
            auto i1 = nir_to_i1(ctx, loc, cond);
            auto if_op = ctx.emit_if(loc, i1);
            {
                mlir::OpBuilder::InsertionGuard g2(b);
                b.setInsertionPointToStart(&if_op.getThenRegion().front());
                if (elt) {
                    elt->nir_codegen(ctx);
                    mlir::Value v = ctx.has_value() ? ctx.pop_value() : mlir::Value{};
                    if (!v) v = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));
                    nir_call_runtime(ctx, loc, "nv_vector_push", {out, v}, {});
                }
                nir_terminate(if_op.getThenRegion().front(), b, loc);
            }
            if (if_op.getElseRegion().empty()) if_op.getElseRegion().emplaceBlock();
            nir_terminate(if_op.getElseRegion().front(), b, loc);
        } else {
            if (elt) {
                elt->nir_codegen(ctx);
                mlir::Value v = ctx.has_value() ? ctx.pop_value() : mlir::Value{};
                if (!v) v = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));
                nir_call_runtime(ctx, loc, "nv_vector_push", {out, v}, {});
            }
        }

        ctx.pop_scope();
        nir_terminate(*body_blk, b, loc);
    }

    b.setInsertionPointAfter(for_op);
    ctx.push_value(out);
}
