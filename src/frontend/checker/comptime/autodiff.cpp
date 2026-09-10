#include "frontend/comptime/autodiff.hpp"

#include "frontend/ast/expressions/arg_node.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "frontend/ast/expressions/call_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include "frontend/ast/expressions/unary_minus_expr_node.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
#include "frontend/ast/statements/return_stmt_node.hpp"

#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace nv {
namespace {

using ExprPtr = std::unique_ptr<Expr>;

ExprPtr num(const std::string& v)  { return std::make_unique<NumericLiteralNode>(v); }
ExprPtr id(const std::string& n)   { return std::make_unique<IdentifierNode>(n); }

ExprPtr bin(const std::string& op, ExprPtr l, ExprPtr r) {
    return std::make_unique<BinaryExprNode>(op, std::move(l), std::move(r));
}

ExprPtr neg(ExprPtr e) { return std::make_unique<UnaryMinusExprNode>(std::move(e)); }

ExprPtr call(const std::string& fn, ExprPtr a) {
    std::vector<std::unique_ptr<ArgNode>> args;
    args.push_back(std::make_unique<ArgNode>(std::string(), std::move(a)));
    return std::make_unique<CallExprNode>(id(fn), std::move(args));
}

// ── numeric literals ────────────────────────────────────────────────────────

bool as_int(const std::string& s, long long& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    long long v = std::strtoll(s.c_str(), &end, 10);
    if (end && *end == '\0') { out = v; return true; }
    return false;
}

bool as_double(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end && *end == '\0') { out = v; return true; }
    return false;
}

bool is_number(const Expr* e) { return e && e->kind == NodeType::NumericLiteral; }

std::string fmt_double(double v) {
    // Keep it short and round-trippable through strtod.
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string(buf);
}

bool is_zero(const Expr* e) {
    if (!is_number(e)) return false;
    auto* n = static_cast<const NumericLiteralNode*>(e);
    long long i;
    double d;
    if (as_int(n->value, i)) return i == 0;
    if (as_double(n->value, d)) return d == 0.0;
    return false;
}

bool is_one(const Expr* e) {
    if (!is_number(e)) return false;
    auto* n = static_cast<const NumericLiteralNode*>(e);
    long long i;
    double d;
    if (as_int(n->value, i)) return i == 1;
    if (as_double(n->value, d)) return d == 1.0;
    return false;
}

bool is_two(const Expr* e) {
    if (!is_number(e)) return false;
    auto* n = static_cast<const NumericLiteralNode*>(e);
    long long i;
    if (as_int(n->value, i)) return i == 2;
    return false;
}

// ── structural helpers ──────────────────────────────────────────────────────

ExprPtr clone_expr(const Expr* e);  // fwd

std::vector<ExprPtr> clone_args(const std::vector<std::unique_ptr<ArgNode>>& args) {
    std::vector<ExprPtr> out;
    for (const auto& a : args) out.push_back(a ? clone_expr(a->value.get()) : nullptr);
    return out;
}

// Deep copy for the node kinds this module can differentiate.
ExprPtr clone_expr(const Expr* e) {
    if (!e) return nullptr;
    switch (e->kind) {
        case NodeType::NumericLiteral:
            return num(static_cast<const NumericLiteralNode*>(e)->value);
        case NodeType::Identifier:
            return id(static_cast<const IdentifierNode*>(e)->symbol);
        case NodeType::UnaryMinusExpression:
            return neg(clone_expr(static_cast<const UnaryMinusExprNode*>(e)->operand.get()));
        case NodeType::BinaryExpression: {
            auto* b = static_cast<const BinaryExprNode*>(e);
            return bin(b->op, clone_expr(b->left.get()), clone_expr(b->right.get()));
        }
        case NodeType::CallExpression: {
            auto* c = static_cast<const CallExprNode*>(e);
            ExprPtr callee = clone_expr(c->caller.get());
            std::vector<std::unique_ptr<ArgNode>> args;
            for (const auto& a : c->args)
                args.push_back(std::make_unique<ArgNode>(a ? a->name : std::string(),
                                                         a ? clone_expr(a->value.get()) : nullptr));
            return std::make_unique<CallExprNode>(std::move(callee), std::move(args));
        }
        default:
            return nullptr;  // unsupported: caller reports it
    }
}

bool expr_equal(const Expr* a, const Expr* b) {
    if (!a || !b || a->kind != b->kind) return false;
    switch (a->kind) {
        case NodeType::NumericLiteral:
            return static_cast<const NumericLiteralNode*>(a)->value ==
                   static_cast<const NumericLiteralNode*>(b)->value;
        case NodeType::Identifier:
            return static_cast<const IdentifierNode*>(a)->symbol ==
                   static_cast<const IdentifierNode*>(b)->symbol;
        case NodeType::UnaryMinusExpression:
            return expr_equal(static_cast<const UnaryMinusExprNode*>(a)->operand.get(),
                              static_cast<const UnaryMinusExprNode*>(b)->operand.get());
        case NodeType::BinaryExpression: {
            auto* x = static_cast<const BinaryExprNode*>(a);
            auto* y = static_cast<const BinaryExprNode*>(b);
            return x->op == y->op && expr_equal(x->left.get(), y->left.get())
                                  && expr_equal(x->right.get(), y->right.get());
        }
        case NodeType::CallExpression: {
            auto* x = static_cast<const CallExprNode*>(a);
            auto* y = static_cast<const CallExprNode*>(b);
            if (!expr_equal(x->caller.get(), y->caller.get())) return false;
            if (x->args.size() != y->args.size()) return false;
            for (size_t i = 0; i < x->args.size(); ++i)
                if (!expr_equal(x->args[i]->value.get(), y->args[i]->value.get())) return false;
            return true;
        }
        default:
            return false;
    }
}

// ── simplifier ──────────────────────────────────────────────────────────────
//
// Constant folding plus the 0/1 identities, and `a + a -> 2*a`. Keeps the
// generated derivative readable instead of a raw product-rule expansion.

ExprPtr fold_binary(const std::string& op, ExprPtr l, ExprPtr r) {
    if (is_number(l.get()) && is_number(r.get())) {
        auto* ln = static_cast<NumericLiteralNode*>(l.get());
        auto* rn = static_cast<NumericLiteralNode*>(r.get());
        long long li, ri;
        if (as_int(ln->value, li) && as_int(rn->value, ri)) {
            if (op == "+") return num(std::to_string(li + ri));
            if (op == "-") return num(std::to_string(li - ri));
            if (op == "*") return num(std::to_string(li * ri));
            if (op == "/" && ri != 0) return num(std::to_string(li / ri));
        }
        double ld, rd;
        if (as_double(ln->value, ld) && as_double(rn->value, rd)) {
            if (op == "+") return num(fmt_double(ld + rd));
            if (op == "-") return num(fmt_double(ld - rd));
            if (op == "*") return num(fmt_double(ld * rd));
            if (op == "/" && rd != 0.0) return num(fmt_double(ld / rd));
        }
    }
    if (op == "+") {
        if (is_zero(l.get())) return r;
        if (is_zero(r.get())) return l;
        if (expr_equal(l.get(), r.get())) return bin("*", num("2"), std::move(l));
    }
    if (op == "-") {
        if (is_zero(r.get())) return l;
        if (is_zero(l.get())) return neg(std::move(r));
    }
    if (op == "*") {
        if (is_zero(l.get()) || is_zero(r.get())) return num("0");
        if (is_one(l.get())) return r;
        if (is_one(r.get())) return l;
    }
    if (op == "/") {
        if (is_zero(l.get())) return num("0");
        if (is_one(r.get())) return l;
    }
    return bin(op, std::move(l), std::move(r));
}

ExprPtr simplify(ExprPtr e) {
    if (!e) return nullptr;
    switch (e->kind) {
        case NodeType::UnaryMinusExpression: {
            auto* u = static_cast<UnaryMinusExprNode*>(e.get());
            ExprPtr inner = simplify(std::move(u->operand));
            if (is_number(inner.get())) {
                auto* n = static_cast<NumericLiteralNode*>(inner.get());
                double d;
                if (as_double(n->value, d)) return num(fmt_double(-d));
            }
            return neg(std::move(inner));
        }
        case NodeType::BinaryExpression: {
            auto* b = static_cast<BinaryExprNode*>(e.get());
            ExprPtr l = simplify(std::move(b->left));
            ExprPtr r = simplify(std::move(b->right));
            return fold_binary(b->op, std::move(l), std::move(r));
        }
        default:
            return e;
    }
}

// ── differentiation ─────────────────────────────────────────────────────────

ExprPtr differentiate(const Expr* e, const std::string& var, std::string& error);

ExprPtr diff_args_call(const CallExprNode* c, const std::string& var, std::string& error) {
    if (!c->caller || c->caller->kind != NodeType::Identifier) {
        error = "autodiff: only direct calls to known functions can be differentiated";
        return nullptr;
    }
    const std::string fn = static_cast<const IdentifierNode*>(c->caller.get())->symbol;
    if (c->args.size() != 1 || !c->args[0] || !c->args[0]->value) {
        error = "autodiff: '" + fn + "' must take exactly one argument";
        return nullptr;
    }
    ExprPtr inner = clone_expr(c->args[0]->value.get());
    if (!inner) { error = "autodiff: unsupported argument expression in '" + fn + "'"; return nullptr; }
    ExprPtr d_inner = differentiate(c->args[0]->value.get(), var, error);
    if (!d_inner) return nullptr;

    // outer = f'(inner) * d_inner
    ExprPtr outer;
    if      (fn == "sin")  outer = call("cos", clone_expr(inner.get()));
    else if (fn == "cos")  outer = neg(call("sin", clone_expr(inner.get())));
    else if (fn == "tan")  outer = bin("/", num("1"), bin("*", call("cos", clone_expr(inner.get())),
                                                                  call("cos", clone_expr(inner.get()))));
    else if (fn == "exp")  outer = call("exp", clone_expr(inner.get()));
    else if (fn == "log")  outer = bin("/", num("1"), std::move(inner));
    else if (fn == "sqrt") outer = bin("/", num("1"),
                                       bin("*", num("2"), call("sqrt", clone_expr(inner.get()))));
    else if (fn == "abs")  outer = num("1");  // sign handled by the caller's domain
    else {
        error = "autodiff: no derivative rule for '" + fn + "'";
        return nullptr;
    }
    return bin("*", std::move(outer), std::move(d_inner));
}

ExprPtr differentiate(const Expr* e, const std::string& var, std::string& error) {
    if (!e) { error = "autodiff: empty expression"; return nullptr; }
    switch (e->kind) {
        case NodeType::NumericLiteral:
            return num("0");
        case NodeType::Identifier: {
            const std::string n = static_cast<const IdentifierNode*>(e)->symbol;
            return num(n == var ? "1" : "0");  // other names are constants here
        }
        case NodeType::UnaryMinusExpression:
            return neg(differentiate(static_cast<const UnaryMinusExprNode*>(e)->operand.get(), var, error));
        case NodeType::BinaryExpression: {
            auto* b = static_cast<const BinaryExprNode*>(e);
            const std::string& op = b->op;
            ExprPtr da = differentiate(b->left.get(), var, error);
            if (!da) return nullptr;
            ExprPtr db = differentiate(b->right.get(), var, error);
            if (!db) return nullptr;

            if (op == "+" || op == "-") return bin(op, std::move(da), std::move(db));
            if (op == "*") {
                // a*db + da*b
                ExprPtr term1 = bin("*", clone_expr(b->left.get()), std::move(db));
                ExprPtr term2 = bin("*", std::move(da), clone_expr(b->right.get()));
                if (!term1 || !term2) { error = "autodiff: unsupported operand in '*'"; return nullptr; }
                return bin("+", std::move(term1), std::move(term2));
            }
            if (op == "/") {
                // (da*b - a*db) / (b*b)
                ExprPtr numr = bin("-",
                    bin("*", std::move(da), clone_expr(b->right.get())),
                    bin("*", clone_expr(b->left.get()), std::move(db)));
                ExprPtr den = bin("*", clone_expr(b->right.get()), clone_expr(b->right.get()));
                return bin("/", std::move(numr), std::move(den));
            }
            error = "autodiff: operator '" + op + "' has no derivative rule";
            return nullptr;
        }
        case NodeType::CallExpression:
            return diff_args_call(static_cast<const CallExprNode*>(e), var, error);
        default:
            error = "autodiff: unsupported expression in the function body";
            return nullptr;
    }
}

} // anonymous namespace

std::unique_ptr<Node> make_derivative(FunctionStmtNode* fn, const std::string& var,
                                      const std::string& new_name, std::string& error) {
    if (!fn) { error = "autodiff: missing function"; return nullptr; }

    // The variable must be one of the parameters.
    bool known = false;
    for (const auto& p : fn->parameters)
        for (const auto& [pname, _] : p.parameter)
            if (pname == var) known = true;
    if (!known) {
        error = "autodiff: '" + var + "' is not a parameter of '" + fn->name + "'";
        return nullptr;
    }

    // Body: exactly one return (optionally preceded by nothing else).
    const ReturnStmtNode* ret = nullptr;
    for (const auto& s : fn->body) {
        if (!s) continue;
        if (s->kind == NodeType::ReturnStatement) {
            ret = static_cast<const ReturnStmtNode*>(s.get());
            break;
        }
        error = "autodiff: only a single `return <expr>` body is supported";
        return nullptr;
    }
    if (!ret || !ret->value) {
        error = "autodiff: '" + fn->name + "' has no return expression to differentiate";
        return nullptr;
    }
    if (ret->value->kind != NodeType::BinaryExpression &&
        ret->value->kind != NodeType::NumericLiteral &&
        ret->value->kind != NodeType::Identifier &&
        ret->value->kind != NodeType::CallExpression &&
        ret->value->kind != NodeType::UnaryMinusExpression) {
        error = "autodiff: unsupported return expression";
        return nullptr;
    }

    ExprPtr deriv = differentiate(ret->value.get(), var, error);
    if (!deriv) return nullptr;
    deriv = simplify(std::move(deriv));
    if (!deriv) { error = "autodiff: simplification failed"; return nullptr; }

    // Same signature, new name.
    std::vector<ParamNode> params;
    for (const auto& p : fn->parameters) params.push_back(p);

    CodeBlock body;
    body.push_back(std::make_unique<ReturnStmtNode>(std::move(deriv)));
    auto out = std::make_unique<FunctionStmtNode>(new_name, std::move(params),
                                                 fn->return_type, std::move(body));
    if (fn->position)
        out->position = std::make_unique<PositionData>(*fn->position);
    return out;
}

} // namespace nv
