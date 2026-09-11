#include "frontend/comptime/derive_generator.hpp"
#include "frontend/comptime/comptime_evaluator.hpp"
#include "frontend/ast/program.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/lexer/lexer_error.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/ast/statements/class_stmt_node.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
#include "frontend/ast/statements/return_stmt_node.hpp"
#include "frontend/ast/expressions/arg_node.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "frontend/ast/expressions/call_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/new_expr_node.hpp"
#include "frontend/ast/expressions/boolean_literal_node.hpp"
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

ExprPtr call2(const std::string& fn, ExprPtr a0, ExprPtr a1) {
    std::vector<std::unique_ptr<ArgNode>> args;
    args.push_back(std::make_unique<ArgNode>("", std::move(a0)));
    args.push_back(std::make_unique<ArgNode>("", std::move(a1)));
    return std::make_unique<CallExprNode>(id(fn), std::move(args));
}

ExprPtr call3(const std::string& fn, ExprPtr a0, ExprPtr a1, ExprPtr a2) {
    std::vector<std::unique_ptr<ArgNode>> args;
    args.push_back(std::make_unique<ArgNode>("", std::move(a0)));
    args.push_back(std::make_unique<ArgNode>("", std::move(a1)));
    args.push_back(std::make_unique<ArgNode>("", std::move(a2)));
    return std::make_unique<CallExprNode>(id(fn), std::move(args));
}

// `target = value;` as a statement (used by the multi-statement derives).
std::unique_ptr<Stmt> assign_stmt(ExprPtr target, ExprPtr value) {
    return std::make_unique<AssignmentExprNode>(std::move(target), "=", std::move(value));
}

std::unique_ptr<ClassMethodNode> make_method_block(const std::string& name,
                                                   std::vector<ParamNode> params,
                                                   const std::string& ret_type,
                                                   CodeBlock body) {
    auto fn = std::make_unique<FunctionStmtNode>(name, std::move(params), ret_type, std::move(body));
    return std::make_unique<ClassMethodNode>(name, "public", std::move(fn), false);
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


// Splices what a user derive returned. The text is ordinary Narval source, so it
// goes through the same lexer/parser as a file — wrapped in a throwaway class, since
// a class body is exactly the grammar for members. Returning source instead of asking
// the author for an AST-building API is what keeps user derives from drifting away
// from the frontend as the language grows.
bool splice_members(const std::string& name, const std::string& src, ClassStmtNode* cls,
                    std::string& error) {
    try {
        Lexer lexer("class __derive_splice {\n" + src + "\n}", "comptime[@derive " + name + "]");
        auto tokens = lexer.tokenize();
        Parser parser;
        auto ast = parser.produce_ast(tokens);
        ClassStmtNode* holder = nullptr;
        if (ast && ast->kind == NodeType::Program) {
            auto* prog = static_cast<Program*>(ast.get());
            for (auto& s : prog->body)
                if (s && s->kind == NodeType::ClassStatement)
                    holder = static_cast<ClassStmtNode*>(s.get());
        }
        if (!holder) {
            error = "@derive(" + name + "): the generated members did not parse as a class body";
            return false;
        }
        if (holder->fields.empty() && holder->methods.empty()) {
            // The parser recovers from a bad member at the next `}` and reports it
            // itself, which can leave an empty body: saying so beats pretending the
            // derive generated nothing on purpose.
            error = "@derive(" + name + "): the generated source has no members "
                    "(the member grammar is `name: type;` or `name(params): type { ... }`)";
            return false;
        }
        for (auto& f : holder->fields) cls->fields.push_back(std::move(f));
        for (auto& m : holder->methods) cls->methods.push_back(std::move(m));
        return true;
    } catch (const std::exception& e) {
        error = "@derive(" + name + "): the generated members are not valid Narval: " + e.what();
        return false;
    }
}

bool has_method(const ClassStmtNode* cls, const std::string& name) {
    for (const auto& m : cls->methods)
        if (m && m->name == name) return true;
    return false;
}

} // anonymous namespace

bool apply_derive(Checker* checker, ClassStmtNode* cls,
                  const std::vector<std::string>& derives, std::string& error,
                  ComptimeEvaluator* ct) {
    if (!cls) return true;

    // Only what the language itself needs to work: the operator protocols
    // (==, <, hash for maps) and the two object-level ones (str for printing, a
    // copy). Serialization, schemas, DB mapping and anything format-specific is
    // stdlib: it belongs in a `comptime def derive_<name>` the author writes, not
    // in the compiler.
    static const std::unordered_set<std::string> known = {
        "eq", "debug", "hash", "ord", "clone",
    };

    for (const std::string& raw : derives) {
        std::string d = raw;
        std::transform(d.begin(), d.end(), d.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (d.empty()) continue;

        // Never override a hand-written method.
        if (d == "eq" && has_method(cls, "__eq__")) continue;
        if (d == "debug" && has_method(cls, "__str__")) continue;
        if (d == "hash" && has_method(cls, "__hash__")) continue;
        if (d == "ord" && has_method(cls, "__lt__")) continue;
        if (d == "clone" && has_method(cls, "clone")) continue;

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
        } else if (d == "clone") {
            // A fresh instance with the same field values. Values are boxed and
            // share their payload, so this is a shallow copy of the fields.
            CodeBlock body;
            body.push_back(assign_stmt(id("out"),
                                       std::make_unique<NewExprNode>(cls->name, cls->name)));
            for (const auto& f : cls->fields)
                body.push_back(assign_stmt(field_of("out", f->name), field_of("self", f->name)));
            body.push_back(std::make_unique<ReturnStmtNode>(id("out")));
            cls->methods.push_back(make_method_block("clone", {}, cls->name, std::move(body)));
        } else {
            // Not a builtin: a USER derive, written in this module as
            //
            //     comptime def derive_log(cls: str, fields: array): str {
            //         out = "";
            //         for f in fields {
            //             out = out + "public def " + f.name + "_text(): str { return str(self." + f.name + "); }\n";
            //         }
            //         return out;
            //     }
            //
            // `cls` is the class name and `fields` holds one descriptor per field
            // (name / type_name / index, the shape `type.fields` uses). Whatever
            // members it returns as source are parsed and spliced into the class here,
            // so a derive is written in Narval, not in the compiler.
            const std::string fn_name = "derive_" + d;
            if (!ct || !ct->has_comptime_func(fn_name)) {
                error = "unknown derive '" + raw + "' (builtin: clone, debug, eq, hash, ord; "
                        "or declare `comptime def " + fn_name + "`)";
                return false;
            }
            std::vector<ComptimeValue> args;
            args.push_back(ComptimeValue::from_str(cls->name));
            // One descriptor per field, with the same shape `type.fields(T)` hands to
            // a comptime function (name / type_name / index), so a derive can do
            // `for f in fields { ... f.name ... f.type_name ... }`.
            std::vector<ComptimeValue> field_vals;
            field_vals.reserve(cls->fields.size());
            for (size_t fi = 0; fi < cls->fields.size(); ++fi) {
                const auto& f = cls->fields[fi];
                std::unordered_map<std::string, ComptimeValue> desc;
                desc["name"]      = ComptimeValue::from_str(f->name);
                desc["type_name"] = ComptimeValue::from_str(f->type);
                desc["index"]     = ComptimeValue::from_int(static_cast<int64_t>(fi));
                field_vals.push_back(ComptimeValue::from_struct(std::move(desc)));
            }
            args.push_back(ComptimeValue::from_array(std::move(field_vals)));

            ComptimeValue res;
            std::string call_error;
            if (!ct->call_comptime_func(fn_name, args, res, call_error)) {
                error = "@derive(" + d + "): " + call_error;
                return false;
            }
            if (res.tag != ComptimeValue::Tag::Str) {
                error = "@derive(" + d + "): " + fn_name +
                        " must return the members as a str";
                return false;
            }
            if (!splice_members(d, res.s_val, cls, error)) return false;
        }
    }

    (void)checker;
    return true;
}

} // namespace nv
