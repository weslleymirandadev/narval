#include "frontend/comptime/derive_generator.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/ast/statements/class_stmt_node.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
#include "frontend/ast/statements/return_stmt_node.hpp"
#include "frontend/ast/expressions/arg_node.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "frontend/ast/expressions/call_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"
#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include "frontend/ast/expressions/param_node.hpp"
#include "frontend/ast/expressions/string_literal_node.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <unordered_set>

namespace nv {
namespace {

using ExprPtr = std::unique_ptr<Expr>;

ExprPtr id(const std::string& name) {
    return std::make_unique<IdentifierNode>(name);
}

ExprPtr str_lit(const std::string& s) {
    return std::make_unique<StringLiteralNode>(s);
}

ExprPtr field_of(const std::string& base, const std::string& field) {
    return std::make_unique<MemberExprNode>(id(base), id(field));
}

ExprPtr call1(const std::string& fn, ExprPtr a0) {
    auto arg = std::make_unique<ArgNode>("", std::move(a0));
    std::vector<std::unique_ptr<ArgNode>> args;
    args.push_back(std::move(arg));
    return std::make_unique<CallExprNode>(id(fn), std::move(args));
}

ExprPtr bin(const std::string& op, ExprPtr l, ExprPtr r) {
    return std::make_unique<BinaryExprNode>(op, std::move(l), std::move(r));
}

ExprPtr int_lit(int v) {
    return std::make_unique<NumericLiteralNode>(std::to_string(v));
}

// Scalar form of a field for hashing: strings hash as their length (there is no
// string hash primitive), numbers as int().
ExprPtr field_scalar(const std::string& base, const ClassFieldNode* f) {
    ExprPtr val = field_of(base, f->name);
    if (f->type == "str" || f->type == "string")
        return call1("len", std::move(val));
    return call1("int", std::move(val));
}

// Human-readable form of a field value: string fields are quoted, everything
// else goes through str().
ExprPtr field_repr(const std::string& base, const ClassFieldNode* f) {
    ExprPtr val = field_of(base, f->name);
    if (f->type == "str" || f->type == "string") {
        return bin("+", bin("+", str_lit("\""), std::move(val)), str_lit("\""));
    }
    return call1("str", std::move(val));
}

ExprPtr chain(const std::string& op, std::vector<ExprPtr> parts) {
    if (parts.empty()) return str_lit("");
    ExprPtr acc = std::move(parts[0]);
    for (size_t i = 1; i < parts.size(); ++i)
        acc = bin(op, std::move(acc), std::move(parts[i]));
    return acc;
}

std::unique_ptr<ClassMethodNode> make_method(const std::string& name,
                                             std::vector<ParamNode> params,
                                             const std::string& ret_type,
                                             ExprPtr ret_expr) {
    CodeBlock body;
    body.push_back(std::make_unique<ReturnStmtNode>(std::move(ret_expr)));
    auto fn = std::make_unique<FunctionStmtNode>(name, std::move(params), ret_type, std::move(body));
    return std::make_unique<ClassMethodNode>(name, "public", std::move(fn), false);
}

ParamNode param(const std::string& name, const std::string& type) {
    std::unordered_map<std::string, std::string> m;
    m[name] = type;
    return ParamNode(m);
}

bool has_method(const ClassStmtNode* cls, const std::string& name) {
    for (const auto& m : cls->methods)
        if (m && m->name == name) return true;
    return false;
}

} // anonymous namespace

bool apply_derive(Checker* checker, ClassStmtNode* cls,
                  const std::vector<std::string>& derives, std::string& error) {
    if (!cls) return true;

    // `clone`/`sql`/`openapi`/`from_json` from the spec are not generated yet:
    // they need first-class Self construction (a multi-statement body) and a
    // JSON parser. Reject them explicitly instead of silently generating
    // something wrong.
    static const std::unordered_set<std::string> known = {
        "eq", "debug", "json", "hash", "ord",
    };

    for (const std::string& raw : derives) {
        std::string d = raw;
        std::transform(d.begin(), d.end(), d.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (d.empty()) continue;

        if (!known.count(d)) {
            error = "unknown derive '" + raw + "' (supported: eq, debug, json, hash, ord)";
            return false;
        }

        // Never override a hand-written method.
        if (d == "eq" && has_method(cls, "__eq__")) continue;
        if (d == "debug" && has_method(cls, "__str__")) continue;
        if (d == "json" && has_method(cls, "to_json")) continue;
        if (d == "hash" && has_method(cls, "__hash__")) continue;
        if (d == "ord" && has_method(cls, "__lt__")) continue;

        if (cls->fields.empty()) {
            error = "@derive(" + d + "): class '" + cls->name + "' has no fields";
            return false;
        }

        if (d == "eq") {
            std::vector<ExprPtr> cmp;
            for (const auto& f : cls->fields)
                cmp.push_back(bin("==", field_of("self", f->name), field_of("other", f->name)));
            ExprPtr all = std::move(cmp[0]);
            for (size_t i = 1; i < cmp.size(); ++i)
                all = bin("&&", std::move(all), std::move(cmp[i]));
            cls->methods.push_back(make_method("__eq__", { param("other", cls->name) }, "bool", std::move(all)));
        } else if (d == "debug") {
            std::vector<ExprPtr> parts;
            parts.push_back(str_lit(cls->name + " { "));
            for (size_t i = 0; i < cls->fields.size(); ++i) {
                if (i) parts.push_back(str_lit(", "));
                parts.push_back(str_lit(cls->fields[i]->name + ": "));
                parts.push_back(field_repr("self", cls->fields[i].get()));
            }
            parts.push_back(str_lit(" }"));
            cls->methods.push_back(make_method("__str__", {}, "str", chain("+", std::move(parts))));
        } else if (d == "json") {
            std::vector<ExprPtr> parts;
            parts.push_back(str_lit("{"));
            for (size_t i = 0; i < cls->fields.size(); ++i) {
                if (i) parts.push_back(str_lit(", "));
                parts.push_back(str_lit("\"" + cls->fields[i]->name + "\": "));
                parts.push_back(field_repr("self", cls->fields[i].get()));
            }
            parts.push_back(str_lit("}"));
            cls->methods.push_back(make_method("to_json", {}, "str", chain("+", std::move(parts))));
        } else if (d == "hash") {
            // h = (((v0 * 31) + v1) * 31) + v2 ... over the field scalars.
            ExprPtr acc = field_scalar("self", cls->fields[0].get());
            for (size_t i = 1; i < cls->fields.size(); ++i)
                acc = bin("+", bin("*", std::move(acc), int_lit(31)),
                          field_scalar("self", cls->fields[i].get()));
            cls->methods.push_back(make_method("__hash__", {}, "int", std::move(acc)));
        } else if (d == "ord") {
            // Lexicographic: (f0 < g0) || (f0 == g0 && (f1 < g1 || (f1 == g1 && ...)))
            size_t n = cls->fields.size();
            ExprPtr acc = bin("<", field_of("self", cls->fields[n - 1]->name),
                                   field_of("other", cls->fields[n - 1]->name));
            for (size_t i = n - 1; i-- > 0;) {
                ExprPtr same = bin("==", field_of("self", cls->fields[i]->name),
                                         field_of("other", cls->fields[i]->name));
                ExprPtr less = bin("<", field_of("self", cls->fields[i]->name),
                                        field_of("other", cls->fields[i]->name));
                acc = bin("||", std::move(less), bin("&&", std::move(same), std::move(acc)));
            }
            cls->methods.push_back(make_method("__lt__", { param("other", cls->name) }, "bool", std::move(acc)));
        }
    }

    (void)checker;
    return true;
}

} // namespace nv
