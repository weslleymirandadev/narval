#pragma once

#include "backend/nir/NIRGenerationContext.hpp"
#include "backend/nir/NarvalOps.h"
#include "frontend/ast/types.hpp"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"

// ── Shared helpers for NIR codegen files ──────────────────────────────────
// Keep these static so each TU gets its own copy without ODR issues.

static inline mlir::Value nir_emit_const(nv::NIRGenerationContext& ctx,
                                          mlir::Location loc,
                                          mlir::Attribute attr) {
    auto vt = ctx.get_narval_value_type();
    return mlir::narval::ConstantOp::create(ctx.get_builder(), loc, vt, attr).getResult();
}

static inline mlir::Value nir_call_runtime(nv::NIRGenerationContext& ctx,
                                            mlir::Location loc,
                                            llvm::StringRef name,
                                            mlir::ValueRange args,
                                            mlir::TypeRange results) {
    auto fn_type = mlir::FunctionType::get(&ctx.get_mlir_context(),
                                            args.getTypes(), results);
    ctx.ensure_runtime_func(name.str(), fn_type);
    auto call = mlir::narval::CallRuntimeOp::create(
        ctx.get_builder(), loc, results,
        mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), name), args);
    return results.empty() ? mlir::Value{} : call.getResults()[0];
}

// Emit all statements in a CodeBlock at the current insertion point.
static inline void nir_emit_body(const CodeBlock& stmts,
                                   nv::NIRGenerationContext& ctx) {
    for (const auto& s : stmts)
        if (s) s->nir_codegen(ctx);
}

// Close a block with narval.yield {} if it has no terminator yet.
static inline void nir_terminate(mlir::Block& block,
                                   mlir::OpBuilder& b,
                                   mlir::Location loc) {
    if (!block.empty() && block.back().hasTrait<mlir::OpTrait::IsTerminator>())
        return;
    b.setInsertionPointToEnd(&block);
    mlir::narval::YieldOp::create(b, loc, mlir::ValueRange{});
}

// Convert a boxed narval.value to i1 via nv_value_is_truthy.
// Uses CallRuntimeOp so the conversion pass handles type lowering automatically.
static inline mlir::Value nir_to_i1(nv::NIRGenerationContext& ctx,
                                      mlir::Location loc,
                                      mlir::Value val) {
    auto i1 = ctx.get_builder().getI1Type();
    auto vt = ctx.get_narval_value_type();
    ctx.ensure_runtime_func("nv_value_is_truthy",
        mlir::FunctionType::get(&ctx.get_mlir_context(), {vt}, {i1}));
    auto call = mlir::narval::CallRuntimeOp::create(
        ctx.get_builder(), loc,
        mlir::TypeRange{i1},
        mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), "nv_value_is_truthy"),
        mlir::ValueRange{val});
    return call.getResults()[0];
}
