#include "frontend/interactive/symbol_collector.hpp"

#include "frontend/ast/ast.hpp"

#include <unordered_set>

namespace narval::frontend::interactive {

namespace {

static bool is_builtin_name(const std::string& s) {
    return s == "write" || s == "read" || s == "json";
}

static void collect_expr_uses(const Expr* expr, std::unordered_set<std::string>& used) {
    if (!expr) return;

    switch (expr->kind) {
        case NodeType::Identifier: {
            auto* id = static_cast<const IdentifierNode*>(expr);
            if (!is_builtin_name(id->symbol)) {
                used.insert(id->symbol);
            }
            break;
        }
        case NodeType::BinaryExpression: {
            auto* b = static_cast<const BinaryExprNode*>(expr);
            collect_expr_uses(b->left.get(), used);
            collect_expr_uses(b->right.get(), used);
            break;
        }
        case NodeType::AssignmentExpression: {
            auto* a = static_cast<const AssignmentExprNode*>(expr);
            collect_expr_uses(a->target.get(), used);
            collect_expr_uses(a->value.get(), used);
            break;
        }
        case NodeType::CallExpression: {
            auto* c = static_cast<const CallExprNode*>(expr);
            collect_expr_uses(c->caller.get(), used);
            for (auto& arg : c->args) {
                collect_expr_uses(arg.get(), used);
            }
            break;
        }
        case NodeType::MemberExpression: {
            auto* m = static_cast<const MemberExprNode*>(expr);
            collect_expr_uses(m->object.get(), used);
            collect_expr_uses(m->property.get(), used);
            break;
        }
        case NodeType::AccessExpression: {
            auto* a = static_cast<const AccessExprNode*>(expr);
            collect_expr_uses(a->expr.get(), used);
            collect_expr_uses(a->index.get(), used);
            break;
        }
        case NodeType::ConditionalExpression: {
            auto* c = static_cast<const ConditionalExprNode*>(expr);
            collect_expr_uses(c->true_expr.get(), used);
            collect_expr_uses(c->condition.get(), used);
            collect_expr_uses(c->false_expr.get(), used);
            break;
        }
        case NodeType::ArrayExpression: {
            auto* a = static_cast<const ArrayExprNode*>(expr);
            for (auto& e : a->elements) collect_expr_uses(e.get(), used);
            break;
        }
        case NodeType::VectorExpression: {
            auto* v = static_cast<const VectorExprNode*>(expr);
            for (auto& e : v->elements) collect_expr_uses(e.get(), used);
            break;
        }
        case NodeType::TupleExpression: {
            auto* t = static_cast<const TupleExprNode*>(expr);
            for (auto& e : t->elements) collect_expr_uses(e.get(), used);
            break;
        }
        case NodeType::Map: {
            auto* m = static_cast<const MapNode*>(expr);
            for (auto& p : m->properties) collect_expr_uses(p.get(), used);
            break;
        }
        case NodeType::KeyValue: {
            auto* kv = static_cast<const KeyValueNode*>(expr);
            collect_expr_uses(kv->key.get(), used);
            collect_expr_uses(kv->value.get(), used);
            break;
        }
        case NodeType::RangeExpression: {
            auto* r = static_cast<const RangeExprNode*>(expr);
            collect_expr_uses(r->start.get(), used);
            collect_expr_uses(r->end.get(), used);
            break;
        }
        case NodeType::ListComprehension: {
            auto* lc = static_cast<const ListCompNode*>(expr);
            collect_expr_uses(lc->elt.get(), used);
            for (const auto& gen : lc->generators) {
                collect_expr_uses(gen.first.get(), used);
                collect_expr_uses(gen.second.get(), used);
            }
            collect_expr_uses(lc->if_cond.get(), used);
            collect_expr_uses(lc->else_expr.get(), used);
            break;
        }
        default:
            break;
    }
}

static void collect_stmt_usage(const Stmt* stmt, SymbolUsage& out, std::unordered_set<std::string>& local_defs) {
    if (!stmt) return;

    switch (stmt->kind) {
        case NodeType::DeclarationStatement: {
            auto* d = static_cast<const DeclarationStmtNode*>(stmt);
            if (auto* id = dynamic_cast<IdentifierNode*>(d->target.get())) {
                out.defined.insert(id->symbol);
                local_defs.insert(id->symbol);
            }
            collect_expr_uses(d->value.get(), out.used);
            break;
        }
        case NodeType::DefStatement: {
            auto* def = static_cast<const DefStmtNode*>(stmt);
            out.defined.insert(def->name);

            std::unordered_set<std::string> params;
            for (const auto& p : def->parameters) {
                for (const auto& kv : p.parameter) {
                    params.insert(kv.first);
                }
            }

            for (const auto& b : def->body) {
                SymbolUsage inner;
                std::unordered_set<std::string> inner_local;
                collect_stmt_usage(b.get(), inner, inner_local);
                for (const auto& u : inner.used) {
                    if (params.find(u) == params.end()) {
                        out.used.insert(u);
                    }
                }
            }
            break;
        }
        case NodeType::AssignmentExpression: {
            // Top-level assignment to an identifier should be treated as a definition
            auto* a = static_cast<const AssignmentExprNode*>(stmt);
            if (auto* id = dynamic_cast<const IdentifierNode*>(a->target.get())) {
                out.defined.insert(id->symbol);
                local_defs.insert(id->symbol);
            }
            collect_expr_uses(a->value.get(), out.used);
            break;
        }
        default: {
            // Many expressions are also Stmt in this AST.
            if (auto* e = dynamic_cast<const Expr*>(stmt)) {
                collect_expr_uses(e, out.used);
            }
            break;
        }
    }
}

} // namespace

SymbolUsage collect_symbol_usage(const Program& program) {
    SymbolUsage out;
    std::unordered_set<std::string> local_defs;

    for (const auto& stmt : program.body) {
        collect_stmt_usage(stmt.get(), out, local_defs);
    }

    // Remove self-defined symbols from used set (unit-local definitions).
    for (const auto& d : out.defined) {
        out.used.erase(d);
    }

    return out;
}

} // namespace narval::frontend::interactive
