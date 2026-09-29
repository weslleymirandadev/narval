#pragma once

#include "backend/nir/NIRGenerationContext.hpp"
#include "backend/nir/NarvalOps.h"
#include "frontend/ast/types.hpp"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"

#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/statements/defer_stmt_node.hpp"
#include "frontend/ast/statements/for_stmt_node.hpp"
#include "frontend/ast/statements/forever_stmt_node.hpp"
#include "frontend/ast/statements/if_statement_node.hpp"
#include "frontend/ast/statements/match_stmt_node.hpp"
#include "frontend/ast/statements/while_stmt_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

// ── Shared helpers for NIR codegen files ──────────────────────────────────
// Keep these static so each TU gets its own copy without ODR issues.
//
// Which names a loop has to carry in and out: every name assigned ANYWHERE in the body,
// nested blocks included. A conditional inside a loop that assigns a variable declared
// outside the loop must have that variable carried, otherwise the assignment only lands in
// the body scope and is thrown away when the iteration ends — which is how
// `for x in v { if cond { acc = acc + x; } }` came out with the accumulator unchanged.
// Both loops use this one walker so the rule cannot differ between them.
static inline std::vector<std::string> nir_assigned_in(const CodeBlock& body) {
    std::vector<std::string> out;
    auto add = [&](const std::string& n) {
        if (!n.empty() && std::find(out.begin(), out.end(), n) == out.end())
            out.push_back(n);
    };
    std::function<void(const CodeBlock&)> walk = [&](const CodeBlock& b) {
        for (const auto& stmt : b) {
            if (!stmt) continue;
            switch (stmt->kind) {
                case NodeType::DeclarationStatement: {
                    auto* d = static_cast<DeclarationStmtNode*>(stmt.get());
                    if (d->target && d->target->kind == NodeType::Identifier)
                        add(static_cast<IdentifierNode*>(d->target.get())->symbol);
                    break;
                }
                case NodeType::AssignmentExpression: {
                    auto* a = static_cast<AssignmentExprNode*>(stmt.get());
                    if (a->target && a->target->kind == NodeType::Identifier)
                        add(static_cast<IdentifierNode*>(a->target.get())->symbol);
                    break;
                }
                case NodeType::IfStatement: {
                    auto* i = static_cast<IfStatementNode*>(stmt.get());
                    walk(i->consequent);
                    walk(i->alternate);
                    break;
                }
                case NodeType::WhileStatement:
                    walk(static_cast<WhileStmtNode*>(stmt.get())->body);
                    break;
                case NodeType::ForStatement: {
                    auto* f = static_cast<ForStmtNode*>(stmt.get());
                    walk(f->body);
                    walk(f->else_block);
                    break;
                }
                case NodeType::ForeverStatement:
                    walk(static_cast<ForeverStmtNode*>(stmt.get())->body);
                    break;
                case NodeType::DeferStatement: {
                    // A `defer` node carries the rest of its block in remaining_body (a
                    // CodeBlock, so the walker can descend) plus its deferred statements in
                    // defer_body (a plain Node list). The assignments that keep a loop going
                    // are in remaining_body — with them missing from the carried set, `defer`
                    // inside a loop looped forever (`i = i + 1` was never carried).
                    auto* d = static_cast<DeferStmtNode*>(stmt.get());
                    walk(d->remaining_body);
                    break;
                }
                case NodeType::MatchStatement: {
                    auto* m = static_cast<MatchStmtNode*>(stmt.get());
                    for (auto& cb : m->bodies) walk(cb);
                    break;
                }
                default:
                    break;
            }
        }
    };
    walk(body);
    return out;
}
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

// A binding of the module also goes into the runtime table: a `def` body is emitted as its
// own func.func (another region), so it cannot use a value that lives in `main.start` — the
// verifier rejected it with "'narval.return' op using value defined outside the region" (B8).
// The body reads the value back with nv_global_get (nir_identifier.cpp).
//
// Both the declaration path and the assignment path call this: at statement level the
// parser hands `K = 5` as an assignment, so patching only the declaration left main.start
// with no store at all.
static inline void nir_store_module_global(nv::NIRGenerationContext& ctx,
                                            mlir::Location loc,
                                            const std::string& name,
                                            mlir::Value value) {
    if (name.empty() || !value) return;
    if (ctx.in_function() && !ctx.is_repl_global(name)) return;   // locals stay SSA

    auto& b  = ctx.get_builder();
    auto  vt = ctx.get_narval_value_type();
    ctx.ensure_runtime_func("nv_global_set",
        mlir::FunctionType::get(&ctx.get_mlir_context(), {vt, vt}, {vt}));
    mlir::Value name_val = nir_emit_const(ctx, loc, b.getStringAttr(name));
    mlir::narval::CallRuntimeOp::create(
        b, loc, mlir::TypeRange{vt},
        mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), "nv_global_set"),
        mlir::ValueRange{name_val, value});
    ctx.note_module_global(name);
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

// Name of the @[no_std] entry function (the symbol the linker jumps to). The codegen
// makes that function leave through the exit syscall: it has no caller to return to,
// so falling off the end of the text kills the process with SIGSEGV right after it
// printed its result.
inline std::string& nir_no_std_entry() {
    static std::string name;
    return name;
}
