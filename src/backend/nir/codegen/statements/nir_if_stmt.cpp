#include "../nir_codegen_utils.hpp"
#include "backend/nir/NarvalOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "frontend/ast/statements/if_statement_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "frontend/ast/expressions/unary_minus_expr_node.hpp"
#include "frontend/ast/expressions/logical_not_expr_node.hpp"
#include "frontend/ast/expressions/access_expr_node.hpp"
#include "frontend/ast/expressions/conditional_expr_node.hpp"
#include "frontend/ast/expressions/call_expr_node.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

// A branch body used to be emitted with its own scope, so `if c { x = 5 }` wrote
// x into that scope and the value vanished when the scope was popped (the outer
// x kept its old value, silently). Threading the values through `narval.if`
// results is not an option here: result-carrying narval.if cannot be legalized
// across the type conversion (see the materialization notes). Instead the
// conditional is if-converted, the same way ternaries already are: the branch
// values become `nv_select(cond, then_value, else_value)` and the `if` itself is
// kept only for its side effects (calls), which run in the taken branch.
//
// Only assignment-only conditional trees with call-free expressions qualify:
// a select evaluates the untaken branch too, so a call would run when it should
// not. Anything else keeps the previous behaviour.

namespace {

using NameList = std::vector<std::string>;

void remember(NameList& names, const std::string& name) {
    if (std::find(names.begin(), names.end(), name) == names.end())
        names.push_back(name);
}

// No calls (and nothing else that may allocate or run user code): the value can
// be computed eagerly without observing anything.
bool expr_is_pure(const Expr* e) {
    if (!e) return true;
    switch (e->kind) {
        case NodeType::NumericLiteral:
        case NodeType::BooleanLiteral:
        case NodeType::CharLiteral:
        case NodeType::StringLiteral:
        case NodeType::NoneLiteral:
        case NodeType::Identifier:
            return true;
        case NodeType::BinaryExpression: {
            auto* n = static_cast<const BinaryExprNode*>(e);
            return expr_is_pure(n->left.get()) && expr_is_pure(n->right.get());
        }
        case NodeType::UnaryMinusExpression:
            return expr_is_pure(static_cast<const UnaryMinusExprNode*>(e)->operand.get());
        case NodeType::LogicalNotExpression:
            return expr_is_pure(static_cast<const LogicalNotExprNode*>(e)->operand.get());
        case NodeType::AccessExpression: {
            auto* n = static_cast<const AccessExprNode*>(e);
            return expr_is_pure(n->expr.get()) && expr_is_pure(n->index.get());
        }
        case NodeType::ConditionalExpression: {
            auto* n = static_cast<const ConditionalExprNode*>(e);
            return expr_is_pure(n->condition.get()) && expr_is_pure(n->true_expr.get()) &&
                   expr_is_pure(n->false_expr.get());
        }
        case NodeType::CallExpression: {
            // `len(x)` and the scalar conversions neither allocate nor mutate, so
            // they are safe to compute for the branch that is not taken.
            auto* c = static_cast<const CallExprNode*>(e);
            if (!c->caller || c->caller->kind != NodeType::Identifier) return false;
            static const char* k_pure[] = {"len", "str", "int", "float", "bool", "char", "abs"};
            const std::string& fn = static_cast<const IdentifierNode*>(c->caller.get())->symbol;
            if (std::find(std::begin(k_pure), std::end(k_pure), fn) == std::end(k_pure))
                return false;
            for (const auto& a : c->args)
                if (a && !expr_is_pure(a->value.get())) return false;
            return true;
        }
        default:
            return false;
    }
}

bool is_call_stmt(const Stmt* s) {
    return s && s->kind == NodeType::CallExpression;
}

// c == nullptr: only the top level of a branch may carry side effects, so a
// nested conditional is required to be pure assignments.
bool is_assignable(const Stmt* s, NameList& assigned) {
    if (!s) return false;
    if (s->kind == NodeType::AssignmentExpression) {
        auto* as = static_cast<const AssignmentExprNode*>(s);
        if (as->op != "=" || !as->target || as->target->kind != NodeType::Identifier)
            return false;
        if (!expr_is_pure(as->value.get())) return false;
        remember(assigned, static_cast<const IdentifierNode*>(as->target.get())->symbol);
        return true;
    }
    return false;
}

bool block_is_convertible(const CodeBlock& body, NameList& assigned, bool top_level) {
    for (const auto& stmt : body) {
        if (!stmt) continue;
        if (is_assignable(stmt.get(), assigned)) continue;
        if (stmt->kind == NodeType::IfStatement) {
            auto* ifs = static_cast<const IfStatementNode*>(stmt.get());
            // Conditions may contain calls: each one is emitted exactly once per
            // conditional conversion (see cond_cache), so nothing runs twice.
            if (!block_is_convertible(ifs->consequent, assigned, false)) return false;
            if (!block_is_convertible(ifs->alternate, assigned, false)) return false;
            continue;
        }
        // Side effects are allowed only directly in the branch body: the
        // preserved `if` re-emits them, and nested calls would need a rebuilt
        // nested conditional.
        if (top_level && is_call_stmt(stmt.get())) continue;
        return false;
    }
    return true;
}

// Value of `name` after running this body, starting from `current`.
mlir::Value emit_value_after(nv::NIRGenerationContext& ctx, mlir::Location loc,
                             const CodeBlock& body, const std::string& name,
                             mlir::Value current, mlir::Type vt,
                             std::map<const Node*, mlir::Value>& cond_cache) {
    for (const auto& stmt : body) {
        if (!stmt) continue;
        if (stmt->kind == NodeType::AssignmentExpression) {
            auto* as = static_cast<AssignmentExprNode*>(stmt.get());
            if (!as->target || as->target->kind != NodeType::Identifier) continue;
            if (static_cast<IdentifierNode*>(as->target.get())->symbol != name) continue;
            if (as->value) {
                as->value->nir_codegen(ctx);
                mlir::Value v = ctx.pop_value();
                if (v) current = v;
            }
            continue;
        }
        if (stmt->kind == NodeType::IfStatement) {
            auto* ifs = static_cast<IfStatementNode*>(stmt.get());
            // One evaluation per conditional, shared by every variable that is
            // selected through it.
            mlir::Value cond;
            auto cached = cond_cache.find(ifs);
            if (cached != cond_cache.end()) {
                cond = cached->second;
            } else {
                if (ifs->condition) {
                    ifs->condition->nir_codegen(ctx);
                    mlir::Value raw = ctx.pop_value();
                    if (raw) cond = nir_to_i1(ctx, loc, raw);
                }
                if (!cond) cond = mlir::arith::ConstantIntOp::create(ctx.get_builder(), loc, 0, 1).getResult();
                cond_cache.emplace(ifs, cond);
            }
            mlir::Value tv = emit_value_after(ctx, loc, ifs->consequent, name, current, vt, cond_cache);
            mlir::Value ev = emit_value_after(ctx, loc, ifs->alternate, name, current, vt, cond_cache);
            ctx.ensure_runtime_func("nv_select",
                mlir::FunctionType::get(&ctx.get_mlir_context(),
                                        {ctx.get_builder().getI1Type(), vt, vt}, {vt}));
            auto sel = mlir::narval::CallRuntimeOp::create(
                ctx.get_builder(), loc, mlir::TypeRange{vt},
                mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), "nv_select"),
                mlir::ValueRange{cond, tv, ev});
            current = sel.getResults()[0];
        }
    }
    return current;
}

void emit_side_effects(const CodeBlock& body, nv::NIRGenerationContext& ctx) {
    for (const auto& stmt : body)
        if (is_call_stmt(stmt.get())) stmt->nir_codegen(ctx);
}

} // anonymous namespace

void IfStatementNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());
    auto  vt  = ctx.get_narval_value_type();

    // Every condition is evaluated exactly once (the top-level one here, nested
    // ones through cond_cache), so a call in a condition is fine.
    NameList assigned;
    bool convertible = block_is_convertible(consequent, assigned, true) &&
                       block_is_convertible(alternate, assigned, true);

    // Plain statement form: no results, so nothing has to escape the region.
    auto emit_plain_if = [&](mlir::Value i1_cond) {
        bool has_else = !alternate.empty();
        auto if_op = ctx.emit_if(loc, i1_cond, {});
        {
            mlir::OpBuilder::InsertionGuard g(b);
            b.setInsertionPointToStart(&if_op.getThenRegion().front());
            ctx.push_scope();
            nir_emit_body(consequent, ctx);
            ctx.pop_scope();
            nir_terminate(if_op.getThenRegion().front(), b, loc);
        }
        {
            mlir::OpBuilder::InsertionGuard g(b);
            b.setInsertionPointToStart(&if_op.getElseRegion().front());
            if (has_else) {
                ctx.push_scope();
                nir_emit_body(alternate, ctx);
                ctx.pop_scope();
            }
            nir_terminate(if_op.getElseRegion().front(), b, loc);
        }
        b.setInsertionPointAfter(if_op);
    };

    mlir::Value cond;
    if (condition) {
        condition->nir_codegen(ctx);
        cond = ctx.pop_value();
    }

    if (!cond) {
        nir_emit_body(consequent, ctx);
        return;
    }

    if (!convertible || assigned.empty()) {
        emit_plain_if(nir_to_i1(ctx, loc, cond));
        return;
    }

    // Snapshot the incoming values before the branch runs.
    std::vector<mlir::Value> incoming;
    incoming.reserve(assigned.size());
    for (const auto& name : assigned) {
        mlir::Value v = ctx.lookup(name);
        if (!v) {  // declared inside the branch: keep the old behaviour
            convertible = false;
            break;
        }
        incoming.push_back(v);
    }

    if (!convertible) {
        emit_plain_if(nir_to_i1(ctx, loc, cond));
        return;
    }

    mlir::Value i1_cond = nir_to_i1(ctx, loc, cond);

    // Side effects first (they run in the taken branch), then the value selects,
    // so a select that reads state the branch changed sees the updated state.
    bool has_side_effects = false;
    for (const auto& stmt : consequent) if (is_call_stmt(stmt.get())) has_side_effects = true;
    for (const auto& stmt : alternate)  if (is_call_stmt(stmt.get())) has_side_effects = true;

    if (has_side_effects) {
        auto if_op = ctx.emit_if(loc, i1_cond, {});
        {
            mlir::OpBuilder::InsertionGuard g(b);
            b.setInsertionPointToStart(&if_op.getThenRegion().front());
            ctx.push_scope();
            emit_side_effects(consequent, ctx);
            ctx.pop_scope();
            nir_terminate(if_op.getThenRegion().front(), b, loc);
        }
        {
            mlir::OpBuilder::InsertionGuard g(b);
            b.setInsertionPointToStart(&if_op.getElseRegion().front());
            ctx.push_scope();
            emit_side_effects(alternate, ctx);
            ctx.pop_scope();
            nir_terminate(if_op.getElseRegion().front(), b, loc);
        }
        b.setInsertionPointAfter(if_op);
    }

    std::map<const Node*, mlir::Value> cond_cache;
    for (size_t i = 0; i < assigned.size(); ++i) {
        mlir::Value tv = emit_value_after(ctx, loc, consequent, assigned[i], incoming[i], vt, cond_cache);
        mlir::Value ev = emit_value_after(ctx, loc, alternate, assigned[i], incoming[i], vt, cond_cache);
        ctx.ensure_runtime_func("nv_select",
            mlir::FunctionType::get(&ctx.get_mlir_context(), {b.getI1Type(), vt, vt}, {vt}));
        auto sel = mlir::narval::CallRuntimeOp::create(
            b, loc, mlir::TypeRange{vt},
            mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), "nv_select"),
            mlir::ValueRange{i1_cond, tv, ev});
        ctx.define(assigned[i], sel.getResults()[0]);
    }
}
