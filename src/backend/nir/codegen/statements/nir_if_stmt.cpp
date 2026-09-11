#include "../nir_codegen_utils.hpp"
#include "backend/nir/NarvalOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "frontend/ast/statements/if_statement_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/statements/while_stmt_node.hpp"
#include "frontend/ast/statements/for_stmt_node.hpp"
#include "frontend/ast/statements/forever_stmt_node.hpp"
#include "frontend/ast/statements/match_stmt_node.hpp"
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

// c == nullptr is no longer needed: purity only decides convertibility, and the
// names a branch writes are collected independently (see collect_assigned).
bool is_assignable(const Stmt* s) {
    if (!s) return false;
    if (s->kind != NodeType::AssignmentExpression) return false;
    auto* as = static_cast<const AssignmentExprNode*>(s);
    if (as->op != "=" || !as->target || as->target->kind != NodeType::Identifier)
        return false;
    return expr_is_pure(as->value.get());
}

bool block_is_convertible(const CodeBlock& body, bool top_level) {
    for (const auto& stmt : body) {
        if (!stmt) continue;
        if (is_assignable(stmt.get())) continue;
        if (stmt->kind == NodeType::IfStatement) {
            auto* ifs = static_cast<const IfStatementNode*>(stmt.get());
            // Conditions may contain calls: each one is emitted exactly once per
            // conditional conversion (see cond_cache), so nothing runs twice.
            if (!block_is_convertible(ifs->consequent, false)) return false;
            if (!block_is_convertible(ifs->alternate, false)) return false;
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

// Names a branch writes, nested loops and conditionals included. This is tracked
// separately from convertibility and must NOT stop at the first statement that
// cannot be if-converted: `if c { t = ...; total = total + 1 }` cannot be converted
// (the declaration of `t` is not a select), yet `total` still has to escape the
// region. Collecting only the convertible prefix left `assigned` empty, the branch
// was emitted as a plain statement, and the assignment to `total` was dropped in
// silence (a loop that accumulated inside such an `if` always read 0).
void collect_assigned(const CodeBlock& body, NameList& assigned) {
    for (const auto& stmt : body) {
        if (!stmt) continue;
        switch (stmt->kind) {
            case NodeType::DeclarationStatement: {
                auto* d = static_cast<const DeclarationStmtNode*>(stmt.get());
                if (d->target && d->target->kind == NodeType::Identifier)
                    remember(assigned, static_cast<const IdentifierNode*>(d->target.get())->symbol);
                break;
            }
            case NodeType::AssignmentExpression: {
                auto* a = static_cast<const AssignmentExprNode*>(stmt.get());
                if (a->target && a->target->kind == NodeType::Identifier)
                    remember(assigned, static_cast<const IdentifierNode*>(a->target.get())->symbol);
                break;
            }
            case NodeType::IfStatement: {
                auto* i = static_cast<const IfStatementNode*>(stmt.get());
                collect_assigned(i->consequent, assigned);
                collect_assigned(i->alternate, assigned);
                break;
            }
            case NodeType::WhileStatement:
                collect_assigned(static_cast<const WhileStmtNode*>(stmt.get())->body, assigned);
                break;
            case NodeType::ForStatement: {
                auto* f = static_cast<const ForStmtNode*>(stmt.get());
                collect_assigned(f->body, assigned);
                collect_assigned(f->else_block, assigned);
                break;
            }
            case NodeType::ForeverStatement:
                collect_assigned(static_cast<const ForeverStmtNode*>(stmt.get())->body, assigned);
                break;
            case NodeType::MatchStatement: {
                auto* m = static_cast<const MatchStmtNode*>(stmt.get());
                for (auto& cb : m->bodies) collect_assigned(cb, assigned);
                break;
            }
            default:
                break;
        }
    }
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
    //
    // Which names escape the branch and whether the branch can be if-converted are
    // two separate questions: the first decides what the `if` yields, the second
    // only whether the selects can replace it. Answering both with one walk made a
    // branch that opens with something non-convertible (a declaration of its own, a
    // loop, a call) report no escaping names at all, and every outer assignment in
    // it vanished.
    NameList written;
    collect_assigned(consequent, written);
    collect_assigned(alternate, written);

    // A name the branch declares for itself stays inside it; only names that already
    // live in the enclosing scope are carried out.
    NameList assigned;
    for (const auto& name : written)
        if (ctx.lookup(name)) assigned.push_back(name);

    bool convertible = block_is_convertible(consequent, true) &&
                       block_is_convertible(alternate, true);

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

    if (assigned.empty()) {
        emit_plain_if(nir_to_i1(ctx, loc, cond));
        return;
    }

    // Snapshot the incoming values before the branch runs.
    std::vector<mlir::Value> incoming;
    incoming.reserve(assigned.size());
    for (const auto& name : assigned) {
        mlir::Value v = ctx.lookup(name);
        if (!v) {  // declared inside the branch: nothing escapes, keep the old form
            emit_plain_if(nir_to_i1(ctx, loc, cond));
            return;
        }
        incoming.push_back(v);
    }

    if (!convertible) {
        // Branches that are not convertible (a nested call, a loop, a compound
        // assignment) still let their values escape: yield one value per variable
        // and define them after the region. A result-carrying narval.if lowers to
        // scf.if with results and gets its types repaired downstream
        // (LowerNarvalControlFlowPass, FixSCFIfTypes), so only the taken branch
        // runs and its writes survive. The select conversion above stays for the
        // convertible case, where it avoids carrying anything through a region.
        std::vector<mlir::Type> result_types(assigned.size(), vt);
        auto if_op = ctx.emit_if(loc, nir_to_i1(ctx, loc, cond), result_types);
        auto fill = [&](mlir::Block& block, const CodeBlock& body, bool runs) {
            mlir::OpBuilder::InsertionGuard g(b);
            b.setInsertionPointToStart(&block);
            if (runs) {
                ctx.push_scope();
                nir_emit_body(body, ctx);
            }
            std::vector<mlir::Value> out;
            out.reserve(assigned.size());
            for (size_t i = 0; i < assigned.size(); ++i) {
                // A branch that does not assign the name keeps the incoming value.
                mlir::Value v = runs ? ctx.lookup(assigned[i]) : mlir::Value();
                out.push_back(v ? v : incoming[i]);
            }
            if (runs) ctx.pop_scope();
            ctx.emit_yield(loc, out);
        };
        fill(if_op.getThenRegion().front(), consequent, true);
        fill(if_op.getElseRegion().front(), alternate, !alternate.empty());
        b.setInsertionPointAfter(if_op);
        for (size_t i = 0; i < assigned.size(); ++i)
            ctx.define(assigned[i], if_op.getResult(i));
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
