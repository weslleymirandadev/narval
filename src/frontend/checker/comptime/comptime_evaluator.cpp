#include "frontend/comptime/comptime_evaluator.hpp"
#include "frontend/ast/program.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/ast/statements/decorator_stmt_node.hpp"
#include "frontend/ast/expressions/access_expr_node.hpp"
#include "frontend/ast/statements/for_stmt_node.hpp"
#include "frontend/ast/statements/while_stmt_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "frontend/ast/expressions/unary_minus_expr_node.hpp"
#include "frontend/ast/expressions/logical_not_expr_node.hpp"
#include "frontend/ast/expressions/conditional_expr_node.hpp"
#include "frontend/ast/expressions/call_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/statements/if_statement_node.hpp"
#include "frontend/ast/statements/forever_stmt_node.hpp"
#include "frontend/ast/statements/return_stmt_node.hpp"
#include "frontend/comptime/c_import.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

namespace nv {
namespace {

// Compile-time loops are bounded so a bad condition cannot hang the compiler.
constexpr size_t MAX_LOOP_ITERATIONS = 100000;

// ── AST reflection (type.ast) ───────────────────────────────────────────────
// Readable kind names for the statements/expressions a macro is likely to walk.
const char* ast_kind_name(NodeType k) {
    switch (k) {
        case NodeType::DeclarationStatement:  return "declaration";
        case NodeType::AssignmentExpression:  return "assignment";
        case NodeType::ReturnStatement:       return "return";
        case NodeType::IfStatement:           return "if";
        case NodeType::WhileStatement:        return "while";
        case NodeType::ForStatement:          return "for";
        case NodeType::ForeverStatement:      return "forever";
        case NodeType::BreakStatement:        return "break";
        case NodeType::ContinueStatement:     return "continue";
        case NodeType::CallExpression:        return "call";
        case NodeType::BinaryExpression:      return "binary";
        case NodeType::UnaryMinusExpression:  return "negate";
        case NodeType::LogicalNotExpression:  return "not";
        case NodeType::AccessExpression:      return "index";
        case NodeType::MemberExpression:      return "member";
        case NodeType::ConditionalExpression: return "ternary";
        case NodeType::NumericLiteral:        return "number";
        case NodeType::StringLiteral:         return "string";
        case NodeType::BooleanLiteral:        return "bool";
        case NodeType::Identifier:            return "identifier";
        default:                              return "other";
    }
}

void ast_push_unique(std::vector<std::string>& out, const std::string& v) {
    if (v.empty()) return;
    if (std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
}

void ast_collect_expr(const Expr* e, std::vector<std::string>& ops,
                      std::vector<std::string>& calls);

// Statements of a body: kind per statement, its line, plus every operator and
// called function used anywhere inside it.
void ast_collect_body(const CodeBlock& body, std::vector<std::string>& kinds,
                      std::vector<int64_t>& lines, std::vector<std::string>& ops,
                      std::vector<std::string>& calls) {
    for (const auto& stmt : body) {
        if (!stmt) continue;
        kinds.push_back(ast_kind_name(stmt->kind));
        lines.push_back(stmt->position ? (int64_t)stmt->position->line : -1);
        switch (stmt->kind) {
            case NodeType::DeclarationStatement: {
                auto* d = static_cast<const DeclarationStmtNode*>(stmt.get());
                ast_collect_expr(d->value.get(), ops, calls);
                break;
            }
            case NodeType::AssignmentExpression: {
                auto* a = static_cast<const AssignmentExprNode*>(stmt.get());
                ast_push_unique(ops, a->op);
                ast_collect_expr(a->value.get(), ops, calls);
                break;
            }
            case NodeType::ReturnStatement: {
                auto* r = static_cast<const ReturnStmtNode*>(stmt.get());
                ast_collect_expr(r->value ? r->value.get() : nullptr, ops, calls);
                break;
            }
            case NodeType::IfStatement: {
                auto* i = static_cast<const IfStatementNode*>(stmt.get());
                ast_collect_expr(i->condition.get(), ops, calls);
                ast_collect_body(i->consequent, kinds, lines, ops, calls);
                ast_collect_body(i->alternate, kinds, lines, ops, calls);
                break;
            }
            case NodeType::WhileStatement: {
                auto* w = static_cast<const WhileStmtNode*>(stmt.get());
                ast_collect_expr(w->condition.get(), ops, calls);
                ast_collect_body(w->body, kinds, lines, ops, calls);
                break;
            }
            case NodeType::ForStatement: {
                auto* f = static_cast<const ForStmtNode*>(stmt.get());
                ast_collect_expr(f->range_start.get(), ops, calls);
                ast_collect_expr(f->range_end.get(), ops, calls);
                ast_collect_body(f->body, kinds, lines, ops, calls);
                break;
            }
            case NodeType::CallExpression:
                // Statement-form calls are stored as expressions; the same cast
                // is used by rewrite_expr_impl below.
                ast_collect_expr(static_cast<Expr*>(stmt.get()), ops, calls);
                break;
            default:
                break;
        }
    }
}

void ast_collect_expr(const Expr* e, std::vector<std::string>& ops,
                      std::vector<std::string>& calls) {
    if (!e) return;
    switch (e->kind) {
        case NodeType::BinaryExpression: {
            auto* n = static_cast<const BinaryExprNode*>(e);
            ast_push_unique(ops, n->op);
            ast_collect_expr(n->left.get(), ops, calls);
            ast_collect_expr(n->right.get(), ops, calls);
            break;
        }
        case NodeType::UnaryMinusExpression:
            ast_push_unique(ops, "-");
            ast_collect_expr(static_cast<const UnaryMinusExprNode*>(e)->operand.get(), ops, calls);
            break;
        case NodeType::LogicalNotExpression:
            ast_push_unique(ops, "!");
            ast_collect_expr(static_cast<const LogicalNotExprNode*>(e)->operand.get(), ops, calls);
            break;
        case NodeType::AccessExpression: {
            auto* n = static_cast<const AccessExprNode*>(e);
            ast_collect_expr(n->expr.get(), ops, calls);
            ast_collect_expr(n->index.get(), ops, calls);
            break;
        }
        case NodeType::ConditionalExpression: {
            auto* n = static_cast<const ConditionalExprNode*>(e);
            ast_collect_expr(n->condition.get(), ops, calls);
            ast_collect_expr(n->true_expr.get(), ops, calls);
            ast_collect_expr(n->false_expr.get(), ops, calls);
            break;
        }
        case NodeType::MemberExpression: {
            auto* n = static_cast<const MemberExprNode*>(e);
            ast_collect_expr(n->object.get(), ops, calls);
            break;
        }
        case NodeType::CallExpression: {
            auto* c = static_cast<const CallExprNode*>(e);
            if (c->caller && c->caller->kind == NodeType::Identifier)
                ast_push_unique(calls, static_cast<const IdentifierNode*>(c->caller.get())->symbol);
            else
                ast_collect_expr(c->caller.get(), ops, calls);
            for (const auto& a : c->args) {
                if (a && a->value) ast_collect_expr(a->value.get(), ops, calls);
            }
            break;
        }
        default:
            break;
    }
}



// Host facts for `target.arch()` / `target.simd()` (spec 5.8). The compiler runs
// on the machine the produced binary targets, so these are compile-time
// constants of the build/CPU.
std::string host_arch() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#elif defined(__riscv)
    return "riscv64";
#else
    return "unknown";
#endif
}

std::string host_simd() {
#if defined(__x86_64__) || defined(__i386__)
#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_cpu_supports("avx512f")) return "avx512";
    if (__builtin_cpu_supports("avx2"))    return "avx2";
    if (__builtin_cpu_supports("avx"))     return "avx";
#endif
    return "sse2";
#elif defined(__aarch64__)
    return "neon";
#else
    return "scalar";
#endif
}

} // anonymous namespace


// Node kinds that carry expressions (used to walk expression statements).
static bool is_expr_kind(NodeType k) {
    switch (k) {
        case NodeType::NumericLiteral:
        case NodeType::BooleanLiteral:
        case NodeType::CharLiteral:
        case NodeType::Identifier:
        case NodeType::BinaryExpression:
        case NodeType::AssignmentExpression:
        case NodeType::Parameter:
        case NodeType::Argument:
        case NodeType::LogicalNotExpression:
        case NodeType::UnaryMinusExpression:
        case NodeType::IncrementExpression:
        case NodeType::DecrementExpression:
        case NodeType::PostIncrementExpression:
        case NodeType::PostDecrementExpression:
        case NodeType::AccessExpression:
        case NodeType::MemberExpression:
        case NodeType::CallExpression:
        case NodeType::Map:
        case NodeType::KeyValue:
        case NodeType::ArrayExpression:
        case NodeType::TupleExpression:
        case NodeType::StringLiteral:
        case NodeType::ConditionalExpression:
        case NodeType::ListComprehension:
        case NodeType::VectorExpression:
        case NodeType::RangeExpression:
        case NodeType::ClosureExpression:
        case NodeType::NewExpression:
        case NodeType::SelfExpression:
        case NodeType::SuperExpression:
        case NodeType::InstanceofExpression:
        case NodeType::NoneLiteral:
        case NodeType::SliceExpression:
        case NodeType::AwaitExpression:
        case NodeType::ComptimeExpr:
        case NodeType::TypeReflectExpr:
        case NodeType::BuiltinCall:
        case NodeType::MacroCall:
        case NodeType::ClassMethod:
        case NodeType::OrExpression:
            return true;
        default:
            return false;
    }
}

// ── ComptimeValue ──────────────────────────────────────────────────────────

ComptimeValue ComptimeValue::from_int(int64_t v) {
    ComptimeValue c; c.tag = ComptimeValue::Tag::Int; c.i_val = v; return c;
}
ComptimeValue ComptimeValue::from_float(double v) {
    ComptimeValue c; c.tag = ComptimeValue::Tag::Float; c.f_val = v; return c;
}
ComptimeValue ComptimeValue::from_bool(bool v) {
    ComptimeValue c; c.tag = ComptimeValue::Tag::Bool; c.b_val = v; return c;
}
ComptimeValue ComptimeValue::from_str(std::string v) {
    ComptimeValue c; c.tag = ComptimeValue::Tag::Str; c.s_val = std::move(v); return c;
}
ComptimeValue ComptimeValue::from_array(std::vector<ComptimeValue> v) {
    ComptimeValue c; c.tag = ComptimeValue::Tag::Array; c.arr_val = std::move(v); return c;
}
ComptimeValue ComptimeValue::from_struct(std::unordered_map<std::string, ComptimeValue> v) {
    ComptimeValue c; c.tag = ComptimeValue::Tag::Struct; c.struct_val = std::move(v); return c;
}
ComptimeValue ComptimeValue::from_type(std::string name) {
    ComptimeValue c; c.tag = ComptimeValue::Tag::Type; c.type_name = std::move(name); return c;
}
ComptimeValue ComptimeValue::none() {
    ComptimeValue c; c.tag = ComptimeValue::Tag::None_; return c;
}
ComptimeValue ComptimeValue::void_value() {
    ComptimeValue c; c.tag = ComptimeValue::Tag::Void; return c;
}

bool ComptimeValue::is_truthy() const {
    switch (tag) {
        case ComptimeValue::Tag::Int: return i_val != 0;
        case ComptimeValue::Tag::Float: return f_val != 0.0;
        case ComptimeValue::Tag::Bool: return b_val;
        case ComptimeValue::Tag::Str: return !s_val.empty();
        case ComptimeValue::Tag::Array: return !arr_val.empty();
        case ComptimeValue::Tag::Struct: return !struct_val.empty();
        case ComptimeValue::Tag::Type: return true;
        default: return false;
    }
}

bool ComptimeValue::is_numeric() const {
    return tag == ComptimeValue::Tag::Int || tag == ComptimeValue::Tag::Float;
}

std::string ComptimeValue::to_string() const {
    switch (tag) {
        case ComptimeValue::Tag::Int: return std::to_string(i_val);
        case ComptimeValue::Tag::Float: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.17g", f_val);
            return buf;
        }
        case ComptimeValue::Tag::Bool: return b_val ? "true" : "false";
        case ComptimeValue::Tag::Str: return s_val;
        case ComptimeValue::Tag::Type: return type_name;
        case ComptimeValue::Tag::None_: return "None";
        case ComptimeValue::Tag::Void: return "void";
        case ComptimeValue::Tag::Array: {
            std::string out = "[";
            for (size_t i = 0; i < arr_val.size(); ++i) {
                if (i) out += ", ";
                out += arr_val[i].to_string();
            }
            return out + "]";
        }
        case ComptimeValue::Tag::Struct: {
            std::string out = "{";
            bool first = true;
            for (const auto& [k, v] : struct_val) {
                if (!first) out += ", ";
                first = false;
                out += k + ": " + v.to_string();
            }
            return out + "}";
        }
    }
    return "";
}

// ── ComptimeEvaluator ──────────────────────────────────────────────────────

ComptimeEvaluator::ComptimeEvaluator(Checker* checker) : checker_(checker) {
    scope_stack_.emplace_back();
}

void ComptimeEvaluator::fail(const std::string& message) {
    fail_code("CE001", message);
}

void ComptimeEvaluator::fail_code(const std::string& code, const std::string& message) {
    if (!failed_) {
        failed_ = true;
        error_code_ = code;
        error_ = message;
    }
}

void ComptimeEvaluator::declare_var(const std::string& name, const ComptimeValue& val) {
    // Always the innermost scope: a parameter of a recursive call must shadow the
    // caller's binding, not overwrite it.
    if (scope_stack_.empty()) scope_stack_.emplace_back();
    scope_stack_.back()[name] = val;
}

void ComptimeEvaluator::push_scope() { scope_stack_.emplace_back(); }
void ComptimeEvaluator::pop_scope()  { if (scope_stack_.size() > 1) scope_stack_.pop_back(); }

void ComptimeEvaluator::set_var(const std::string& name, const ComptimeValue& val) {
    if (scope_stack_.empty()) scope_stack_.emplace_back();
    // Assign to the binding where it already lives, not unconditionally to the
    // innermost scope: loop bodies push a scope per iteration, and writing to
    // that scope made `i = i + 1` invisible to the loop condition (infinite
    // loop, only stopped by the iteration cap).
    for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            found->second = val;
            return;
        }
    }
    scope_stack_.back()[name] = val;
}

ComptimeValue* ComptimeEvaluator::lookup_var(const std::string& name) {
    for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return &found->second;
    }
    return nullptr;
}

void ComptimeEvaluator::register_func(ComptimeFuncNode* node) {
    if (node) comptime_funcs_[node->name] = node;
}

void ComptimeEvaluator::register_func_signature(const std::string& name,
                                                std::vector<bool> comptime_flags) {
    func_comptime_params_[name] = std::move(comptime_flags);
}

ComptimeValue ComptimeEvaluator::eval_range(RangeExprNode* node) {
    ComptimeValue start = node->start ? eval(node->start.get()) : ComptimeValue::from_int(0);
    ComptimeValue end = node->end ? eval(node->end.get()) : ComptimeValue::from_int(0);
    if (!start.is_numeric() || !end.is_numeric()) {
        fail("comptime range bounds must be numeric");
        return ComptimeValue::none();
    }
    int64_t lo = (start.tag == ComptimeValue::Tag::Int) ? start.i_val : static_cast<int64_t>(start.f_val);
    int64_t hi = (end.tag == ComptimeValue::Tag::Int) ? end.i_val : static_cast<int64_t>(end.f_val);
    if (node->inclusive) hi += 1;

    std::vector<ComptimeValue> out;
    for (int64_t i = lo; i < hi; ++i) out.push_back(ComptimeValue::from_int(i));
    return ComptimeValue::from_array(std::move(out));
}

ComptimeValue ComptimeEvaluator::eval_binary(BinaryExprNode* node) {
    const std::string& op = node->op;

    // `&&` and `||` short-circuit: the right side is evaluated only when it can
    // still change the result. Evaluating both eagerly made the usual guard
    // `i < len(s) && s[i] != c` index past the end of the string.
    if (op == "&&" || op == "and" || op == "||" || op == "or") {
        bool is_and = (op == "&&" || op == "and");
        ComptimeValue left = eval(node->left.get());
        if (failed_) return ComptimeValue::none();
        bool left_true = left.is_truthy();
        if (is_and ? !left_true : left_true) return ComptimeValue::from_bool(left_true);
        ComptimeValue right = eval(node->right.get());
        if (failed_) return ComptimeValue::none();
        return ComptimeValue::from_bool(right.is_truthy());
    }

    ComptimeValue l = eval(node->left.get());
    ComptimeValue r = eval(node->right.get());

    // String concatenation and string comparison.
    if (l.tag == ComptimeValue::Tag::Str || r.tag == ComptimeValue::Tag::Str) {
        if (op == "+") {
            return ComptimeValue::from_str(l.to_string() + r.to_string());
        }
        if (op == "==") return ComptimeValue::from_bool(l.to_string() == r.to_string());
        if (op == "!=") return ComptimeValue::from_bool(l.to_string() != r.to_string());
        fail("unsupported comptime operator '" + op + "' for strings");
        return ComptimeValue::none();
    }

    // Boolean operators.
    if (op == "&&" || op == "and") return ComptimeValue::from_bool(l.is_truthy() && r.is_truthy());
    if (op == "||" || op == "or")  return ComptimeValue::from_bool(l.is_truthy() || r.is_truthy());
    if (op == "==") {
        if (l.is_numeric() && r.is_numeric())
            return ComptimeValue::from_bool(l.tag == ComptimeValue::Tag::Float || r.tag == ComptimeValue::Tag::Float
                                                ? l.f_val == r.f_val || l.i_val == r.i_val
                                                : l.i_val == r.i_val);
        return ComptimeValue::from_bool(l.is_truthy() == r.is_truthy());
    }
    if (op == "!=") {
        if (l.is_numeric() && r.is_numeric()) {
            if (l.tag == ComptimeValue::Tag::Int && r.tag == ComptimeValue::Tag::Int) return ComptimeValue::from_bool(l.i_val != r.i_val);
            double lv = (l.tag == ComptimeValue::Tag::Int) ? static_cast<double>(l.i_val) : l.f_val;
            double rv = (r.tag == ComptimeValue::Tag::Int) ? static_cast<double>(r.i_val) : r.f_val;
            return ComptimeValue::from_bool(lv != rv);
        }
        return ComptimeValue::from_bool(l.is_truthy() != r.is_truthy());
    }
    if (!l.is_numeric() || !r.is_numeric()) {
        fail("comptime operator '" + op + "' requires numeric operands");
        return ComptimeValue::none();
    }

    bool as_float = (l.tag == ComptimeValue::Tag::Float || r.tag == ComptimeValue::Tag::Float);
    double lf = (l.tag == ComptimeValue::Tag::Int) ? static_cast<double>(l.i_val) : l.f_val;
    double rf = (r.tag == ComptimeValue::Tag::Int) ? static_cast<double>(r.i_val) : r.f_val;
    int64_t li = l.i_val, ri = r.i_val;

    auto cmp = [&](bool c) { return ComptimeValue::from_bool(c); };

    if (op == "<")  return cmp(as_float ? lf <  rf : li <  ri);
    if (op == ">")  return cmp(as_float ? lf >  rf : li >  ri);
    if (op == "<=") return cmp(as_float ? lf <= rf : li <= ri);
    if (op == ">=") return cmp(as_float ? lf >= rf : li >= ri);
    if (op == "+")  return as_float ? ComptimeValue::from_float(lf + rf) : ComptimeValue::from_int(li + ri);
    if (op == "-")  return as_float ? ComptimeValue::from_float(lf - rf) : ComptimeValue::from_int(li - ri);
    if (op == "*")  return as_float ? ComptimeValue::from_float(lf * rf) : ComptimeValue::from_int(li * ri);

    if (op == "/") {
        if (as_float) {
            if (rf == 0.0) { fail("comptime division by zero"); return ComptimeValue::none(); }
            return ComptimeValue::from_float(lf / rf);
        }
        if (ri == 0) { fail("comptime division by zero"); return ComptimeValue::none(); }
        return ComptimeValue::from_int(li / ri);
    }
    if (op == "//") {
        if (as_float) {
            if (rf == 0.0) { fail("comptime division by zero"); return ComptimeValue::none(); }
            return ComptimeValue::from_float(std::floor(lf / rf));
        }
        if (ri == 0) { fail("comptime division by zero"); return ComptimeValue::none(); }
        int64_t q = li / ri;
        if ((li % ri != 0) && ((li < 0) != (ri < 0))) q -= 1;
        return ComptimeValue::from_int(q);
    }
    if (op == "%") {
        if (as_float) { fail("comptime '%' requires integer operands"); return ComptimeValue::none(); }
        if (ri == 0) { fail("comptime modulo by zero"); return ComptimeValue::none(); }
        int64_t rem = li % ri;
        if (rem != 0 && ((rem < 0) != (ri < 0))) rem += ri;
        return ComptimeValue::from_int(rem);
    }
    if (op == "**") {
        if (as_float) return ComptimeValue::from_float(std::pow(lf, rf));
        if (ri < 0) { fail("comptime '**' with negative exponent"); return ComptimeValue::none(); }
        int64_t acc = 1;
        for (int64_t i = 0; i < ri; ++i) acc *= li;
        return ComptimeValue::from_int(acc);
    }

    fail("unsupported comptime operator '" + op + "'");
    return ComptimeValue::none();
}

ComptimeValue ComptimeEvaluator::eval_member(MemberExprNode* node) {
    // <struct_value>.<field>, where the object may also be an inline expression
    // such as `type.ast(f).name` (a reflection descriptor is a Struct, and a
    // Struct cannot be stored in a `comptime` variable as a runtime literal).
    if (!node->property || node->property->kind != NodeType::Identifier) {
        fail("unsupported comptime member access");
        return ComptimeValue::none();
    }
    const std::string& field =
        static_cast<IdentifierNode*>(node->property.get())->symbol;

    ComptimeValue obj = eval(node->object.get());
    if (failed_) return ComptimeValue::none();
    if (obj.tag == ComptimeValue::Tag::Struct) {
        auto found = obj.struct_val.find(field);
        if (found != obj.struct_val.end()) return found->second;
        fail("comptime struct has no field '" + field + "'");
        return ComptimeValue::none();
    }
    fail("comptime member access on a non-struct value");
    return ComptimeValue::none();
}

ComptimeValue ComptimeEvaluator::eval_type_reflect(TypeReflectExprNode* node) {
    const std::string& fn = node->fn;
    const std::string& type = node->target_type;
    if (type.empty()) {
        fail("type." + fn + " requires a type argument");
        return ComptimeValue::none();
    }

    if (fn == "name") return ComptimeValue::from_str(type);

    if (fn == "ast") {
        // AST reflection (COMPTIME_SPEC 5.9 / roadmap 17): a navigable
        // description of a `comptime def`. Its node is the one the evaluator
        // registered, so the body is the source the author wrote (nested blocks
        // included). Building new functions from this description is not
        // supported yet - only inspection.
        ComptimeFuncNode* target = nullptr;
        auto direct = comptime_funcs_.find(type);
        if (direct != comptime_funcs_.end()) {
            target = direct->second;
        } else {
            auto macro = comptime_funcs_.find(type + "!");
            if (macro != comptime_funcs_.end()) target = macro->second;
        }
        if (!target) {
            fail("type.ast('" + type + "'): only comptime functions can be inspected "
                 "(declare it with `comptime def`)");
            return ComptimeValue::none();
        }

        std::vector<std::string> kinds;
        std::vector<int64_t> lines;
        std::vector<std::string> ops;
        std::vector<std::string> calls;
        ast_collect_body(target->body, kinds, lines, ops, calls);

        auto str_array = [](const std::vector<std::string>& v) {
            std::vector<ComptimeValue> out;
            out.reserve(v.size());
            for (const auto& x : v) out.push_back(ComptimeValue::from_str(x));
            return ComptimeValue::from_array(std::move(out));
        };
        auto int_array = [](const std::vector<int64_t>& v) {
            std::vector<ComptimeValue> out;
            out.reserve(v.size());
            for (int64_t x : v) out.push_back(ComptimeValue::from_int(x));
            return ComptimeValue::from_array(std::move(out));
        };

        std::vector<ComptimeValue> params;
        std::vector<std::string> param_names;
        std::vector<std::string> param_types;
        int64_t index = 0;
        for (const auto& p : target->parameters) {
            for (const auto& [pname, ptype] : p.parameter) {
                std::unordered_map<std::string, ComptimeValue> entry;
                entry["index"] = ComptimeValue::from_int(index++);
                entry["name"] = ComptimeValue::from_str(pname);
                entry["type_name"] = ComptimeValue::from_str(ptype);
                params.push_back(ComptimeValue::from_struct(std::move(entry)));
                param_names.push_back(pname);
                param_types.push_back(ptype);
            }
        }

        std::unordered_map<std::string, ComptimeValue> desc;
        desc["name"] = ComptimeValue::from_str(target->name);
        desc["return_type"] = ComptimeValue::from_str(target->return_type);
        desc["is_macro"] = ComptimeValue::from_bool(!target->name.empty() &&
                                                    target->name.back() == '!');
        desc["statement_count"] = ComptimeValue::from_int((int64_t)target->body.size());
        // Structured form, plus the flat pair: `comptime for` expands by
        // substituting literals, and an element that is itself a struct cannot
        // be materialised (yet), so the flat arrays are what macros iterate.
        desc["params"] = ComptimeValue::from_array(std::move(params));
        desc["param_names"] = str_array(param_names);
        desc["param_types"] = str_array(param_types);
        desc["body_kinds"] = str_array(kinds);
        desc["lines"] = int_array(lines);
        desc["expr_ops"] = str_array(ops);
        desc["calls"] = str_array(calls);
        return ComptimeValue::from_struct(std::move(desc));
    }

    std::shared_ptr<Type> ty;
    if (checker_) {
        auto it = checker_->types.find(type);
        if (it != checker_->types.end()) ty = it->second;
    }
    if (!ty) {
        fail("type." + fn + ": unknown type '" + type + "'");
        return ComptimeValue::none();
    }

    if (fn == "kind") {
        switch (ty->kind) {
            case Kind::CLASS:     return ComptimeValue::from_str("class");
            case Kind::INTERFACE: return ComptimeValue::from_str("interface");
            case Kind::ENUM:      return ComptimeValue::from_str("enum");
            case Kind::FUNCTION:  return ComptimeValue::from_str("function");
            case Kind::ARRAY:     return ComptimeValue::from_str("array");
            case Kind::VECTOR:    return ComptimeValue::from_str("vector");
            case Kind::MAP:       return ComptimeValue::from_str("map");
            case Kind::TUPLE:     return ComptimeValue::from_str("tuple");
            default:              return ComptimeValue::from_str("primitive");
        }
    }

    if (ty->kind != Kind::CLASS) {
        fail("type." + fn + " requires a class type, got '" + type + "'");
        return ComptimeValue::none();
    }
    auto cls = std::static_pointer_cast<Class>(ty);

    if (fn == "has_field") {
        return ComptimeValue::from_bool(cls->fields.count(node->extra_arg) > 0);
    }

    if (fn == "fields") {
        std::vector<ComptimeValue> out;
        // Sort by declaration order is not tracked; emit a stable order.
        std::vector<std::string> names;
        for (const auto& [name, _] : cls->fields) names.push_back(name);
        std::sort(names.begin(), names.end());
        int64_t index = 0;
        for (const auto& name : names) {
            std::unordered_map<std::string, ComptimeValue> field;
            field["name"] = ComptimeValue::from_str(name);
            field["type_name"] = ComptimeValue::from_str(cls->fields[name]->toString());
            field["index"] = ComptimeValue::from_int(index++);
            out.push_back(ComptimeValue::from_struct(std::move(field)));
        }
        return ComptimeValue::from_array(std::move(out));
    }

    if (fn == "methods") {
        std::vector<ComptimeValue> out;
        std::vector<std::string> names;
        for (const auto& [name, _] : cls->methods) names.push_back(name);
        std::sort(names.begin(), names.end());
        for (const auto& name : names) {
            std::unordered_map<std::string, ComptimeValue> m;
            m["name"] = ComptimeValue::from_str(name);
            m["return_type"] = ComptimeValue::from_str(cls->methods[name]->toString());
            out.push_back(ComptimeValue::from_struct(std::move(m)));
        }
        return ComptimeValue::from_array(std::move(out));
    }

    fail("unknown type reflection function 'type." + fn + "'");
    return ComptimeValue::none();
}

// Zig-style compile-time builtins: @typeName/@kind/@hasField/@fieldNames/
// @fields/@methods/@sizeOf/@TypeOf/@compileError.
ComptimeValue ComptimeEvaluator::eval_builtin(BuiltinCallNode* node) {
    const std::string& fn = node->name;

    auto type_arg = [&](size_t i) -> std::string {
        if (i >= node->args.size() || !node->args[i]) return "";
        Expr* a = node->args[i].get();
        if (a->kind == NodeType::Identifier) return static_cast<IdentifierNode*>(a)->symbol;
        return eval(a).to_string();
    };
    auto str_arg = [&](size_t i) -> std::string {
        if (i >= node->args.size() || !node->args[i]) return "";
        return eval(node->args[i].get()).to_string();
    };

    if (fn == "compileError") {
        fail("@compileError: " + str_arg(0));
        return ComptimeValue::none();
    }
    if (fn == "typeName") return ComptimeValue::from_str(type_arg(0));
    if (fn == "TypeOf") {
        if (node->args.empty() || !node->args[0]) {
            fail("@TypeOf requires an argument");
            return ComptimeValue::none();
        }
        ComptimeValue v = eval(node->args[0].get());
        switch (v.tag) {
            case ComptimeValue::Tag::Int:    return ComptimeValue::from_str("int");
            case ComptimeValue::Tag::Float:  return ComptimeValue::from_str("float");
            case ComptimeValue::Tag::Bool:   return ComptimeValue::from_str("bool");
            case ComptimeValue::Tag::Str:    return ComptimeValue::from_str("str");
            case ComptimeValue::Tag::Array:  return ComptimeValue::from_str("array");
            case ComptimeValue::Tag::Type:   return ComptimeValue::from_str(v.type_name);
            case ComptimeValue::Tag::None_:  return ComptimeValue::from_str("None");
            default:                         return ComptimeValue::from_str("unknown");
        }
    }
    if (fn == "sizeOf") {
        std::string t = type_arg(0);
        if (t == "int" || t == "float" || t == "str" || t == "usize" || t == "isize")
            return ComptimeValue::from_int(8);
        if (t == "bool" || t == "char" || t == "u8" || t == "i8")
            return ComptimeValue::from_int(1);
        if (t == "i16" || t == "u16") return ComptimeValue::from_int(2);
        if (t == "i32" || t == "u32") return ComptimeValue::from_int(4);
        fail("@sizeOf: unsupported type '" + t + "'");
        return ComptimeValue::none();
    }

    // Reflections implemented on top of eval_type_reflect.
    if (fn == "kind" || fn == "hasField" || fn == "fields" || fn == "methods") {
        std::string mapped = (fn == "hasField") ? "has_field" : fn;
        TypeReflectExprNode refl(mapped, type_arg(0), str_arg(1));
        return eval_type_reflect(&refl);
    }
    if (fn == "fieldNames") {
        TypeReflectExprNode refl("fields", type_arg(0));
        ComptimeValue fields = eval_type_reflect(&refl);
        if (fields.tag != ComptimeValue::Tag::Array) return fields;
        std::vector<ComptimeValue> names;
        for (auto& f : fields.arr_val) {
            auto it = f.struct_val.find("name");
            names.push_back(it != f.struct_val.end() ? it->second : ComptimeValue::from_str(""));
        }
        return ComptimeValue::from_array(std::move(names));
    }

    fail("unknown compile-time builtin '@" + fn + "'");
    return ComptimeValue::none();
}

// `name! { verbatim body }` — hands the raw text to the matching
// `comptime def name!(src: str)` macro and returns its value.
ComptimeValue ComptimeEvaluator::eval_macro(MacroCallNode* node) {
    const std::string key = node->name + "!";
    auto it = comptime_funcs_.find(key);
    if (it == comptime_funcs_.end()) {
        fail("'" + key + "' is not a comptime macro (define it with "
             "`comptime def " + key + "(src: str): ...`)");
        return ComptimeValue::none();
    }
    if (call_depth_ >= MAX_DEPTH) {
        fail("comptime recursion limit exceeded in macro '" + key + "'");
        return ComptimeValue::none();
    }
    ComptimeFuncNode* fn = it->second;

    push_scope();
    // Bind the verbatim body to the macro's first parameter.
    if (!fn->parameters.empty()) {
        std::string pname;
        for (const auto& [k, _] : fn->parameters[0].parameter) { pname = k; break; }
        if (!pname.empty()) declare_var(pname, ComptimeValue::from_str(node->raw_src));
    }
    ++call_depth_;
    ComptimeValue result = eval_block(fn->body);
    --call_depth_;
    pop_scope();
    return result;
}

ComptimeValue ComptimeEvaluator::eval_call(CallExprNode* node) {
    // target.arch() / target.simd() — comptime facts about the target platform.
    if (node->caller && node->caller->kind == NodeType::MemberExpression) {
        auto* mem = static_cast<MemberExprNode*>(node->caller.get());
        if (mem->object && mem->object->kind == NodeType::Identifier &&
            mem->property && mem->property->kind == NodeType::Identifier &&
            static_cast<IdentifierNode*>(mem->object.get())->symbol == "target") {
            const std::string fn = static_cast<IdentifierNode*>(mem->property.get())->symbol;
            if (fn == "arch") return ComptimeValue::from_str(host_arch());
            if (fn == "simd") return ComptimeValue::from_str(host_simd());
            fail("unknown target builtin 'target." + fn + "' (expected arch or simd)");
            return ComptimeValue::none();
        }
    }


    // type.<fn>(T[, extra]) — reflection.
    if (node->caller && node->caller->kind == NodeType::MemberExpression) {
        auto* m = static_cast<MemberExprNode*>(node->caller.get());
        if (m->object && m->object->kind == NodeType::Identifier &&
            static_cast<IdentifierNode*>(m->object.get())->symbol == "type" &&
            m->property && m->property->kind == NodeType::Identifier) {
            std::string fn = static_cast<IdentifierNode*>(m->property.get())->symbol;
            std::string target;
            std::string extra;
            if (!node->args.empty() && node->args[0] && node->args[0]->value) {
                Expr* a0 = node->args[0]->value.get();
                if (a0->kind == NodeType::Identifier)
                    target = static_cast<IdentifierNode*>(a0)->symbol;
                else
                    target = eval(a0).to_string();
            }
            if (node->args.size() > 1 && node->args[1] && node->args[1]->value)
                extra = eval(node->args[1]->value.get()).to_string();
            TypeReflectExprNode refl(fn, target, extra);
            if (node->position) refl.position = std::make_unique<PositionData>(*node->position);
            return eval_type_reflect(&refl);
        }
        fail("unsupported method call in comptime context");
        return ComptimeValue::none();
    }

    if (!node->caller || node->caller->kind != NodeType::Identifier) {
        fail("only named comptime functions can be called in comptime context");
        return ComptimeValue::none();
    }

    const std::string name = static_cast<IdentifierNode*>(node->caller.get())->symbol;

    // comptime import_c("header.h"[, link: "lib"]) — parse a C header now and
    // register its prototypes for codegen (see hermes skill / COMPTIME_SPEC 5.5).
    if (name == "import_c") {
        std::string path;
        std::string link;
        if (!node->args.empty() && node->args[0] && node->args[0]->value &&
            node->args[0]->value->kind == NodeType::StringLiteral)
            path = static_cast<StringLiteralNode*>(node->args[0]->value.get())->value;
        for (size_t i = 1; i < node->args.size(); ++i) {
            if (node->args[i] && node->args[i]->name == "link" && node->args[i]->value)
                link = eval(node->args[i]->value.get()).to_string();
        }
        if (path.empty()) {
            fail("import_c requires a header path, e.g. comptime import_c(\"stdio.h\")");
            return ComptimeValue::none();
        }
        std::string err;
        if (!import_c_header(checker_, path, link, err)) {
            fail(err);
            return ComptimeValue::none();
        }
        return ComptimeValue::void_value();
    }

    // Builtins that are safe to use inside comptime function bodies. The checker
    // knows about these too, but comptime bodies are folded away before it runs,
    // so the evaluator needs its own handling.
    if (name == "len" || name == "int" || name == "float" || name == "str" || name == "bool") {
        if (node->args.empty() || !node->args[0] || !node->args[0]->value) {
            fail(name + "() requires an argument in comptime context");
            return ComptimeValue::none();
        }
        ComptimeValue v = eval(node->args[0]->value.get());
        if (failed_) return ComptimeValue::none();
        if (name == "len") {
            switch (v.tag) {
                case ComptimeValue::Tag::Str:    return ComptimeValue::from_int((int64_t)v.s_val.size());
                case ComptimeValue::Tag::Array:  return ComptimeValue::from_int((int64_t)v.arr_val.size());
                case ComptimeValue::Tag::Struct: return ComptimeValue::from_int((int64_t)v.struct_val.size());
                default:
                    fail("len() expects a string, array or struct in comptime context");
                    return ComptimeValue::none();
            }
        }
        if (name == "int") {
            if (v.tag == ComptimeValue::Tag::Float) return ComptimeValue::from_int((int64_t)v.f_val);
            if (v.tag == ComptimeValue::Tag::Bool)  return ComptimeValue::from_int(v.b_val ? 1 : 0);
            if (v.tag == ComptimeValue::Tag::Str)   return ComptimeValue::from_int(strtoll(v.s_val.c_str(), nullptr, 10));
            return v;
        }
        if (name == "float") {
            if (v.tag == ComptimeValue::Tag::Int) return ComptimeValue::from_float((double)v.i_val);
            if (v.tag == ComptimeValue::Tag::Str) return ComptimeValue::from_float(strtod(v.s_val.c_str(), nullptr));
            return v;
        }
        if (name == "str") return ComptimeValue::from_str(v.to_string());
        return ComptimeValue::from_bool(v.is_truthy());  // bool
    }

    auto it = comptime_funcs_.find(name);
    if (it == comptime_funcs_.end()) {
        fail("'" + name + "' is not a comptime function (runtime calls are not allowed in comptime context)");
        return ComptimeValue::none();
    }
    if (call_depth_ >= MAX_DEPTH) {
        fail("comptime recursion limit (" + std::to_string(MAX_DEPTH) + ") exceeded in '" + name + "'");
        return ComptimeValue::none();
    }
    ComptimeFuncNode* fn = it->second;

    std::vector<ComptimeValue> arg_vals;
    arg_vals.reserve(node->args.size());
    for (auto& a : node->args) arg_vals.push_back(eval(a->value.get()));

    push_scope();
    size_t i = 0;
    for (auto& p : fn->parameters) {
        std::string pname;
        for (const auto& [k, _] : p.parameter) { pname = k; break; }
        if (i < arg_vals.size()) declare_var(pname, arg_vals[i]);
        ++i;
    }
    ++call_depth_;
    ComptimeValue result = eval_block(fn->body);
    --call_depth_;
    pop_scope();
    return result;
}

ComptimeValue ComptimeEvaluator::eval(Expr* expr) {
    if (!expr) {
        fail("empty comptime expression");
        return ComptimeValue::none();
    }
    // Remember where we are: on failure this is the location the diagnostic
    // points at (the innermost expression evaluated before the error). Store a
    // copy of the position, not the node — expansion may free the node later.
    if (expr->position) error_pos_ = std::make_unique<PositionData>(*expr->position);
    switch (expr->kind) {
        case NodeType::NumericLiteral: {
            auto* n = static_cast<NumericLiteralNode*>(expr);
            const std::string& s = n->value;
            if (s.find('.') != std::string::npos || s.find('e') != std::string::npos ||
                s.find('E') != std::string::npos) {
                try { return ComptimeValue::from_float(std::stod(s)); }
                catch (...) { fail("invalid comptime float literal '" + s + "'"); return ComptimeValue::none(); }
            }
            try { return ComptimeValue::from_int(std::stoll(s)); }
            catch (...) {
                try { return ComptimeValue::from_float(std::stod(s)); }
                catch (...) { fail("invalid comptime numeric literal '" + s + "'"); return ComptimeValue::none(); }
            }
        }
        case NodeType::StringLiteral:
            return ComptimeValue::from_str(static_cast<StringLiteralNode*>(expr)->value);
        case NodeType::BooleanLiteral:
            return ComptimeValue::from_bool(static_cast<BooleanLiteralNode*>(expr)->value);
        case NodeType::NoneLiteral:
            return ComptimeValue::none();
        case NodeType::Identifier: {
            auto* id = static_cast<IdentifierNode*>(expr);
            if (ComptimeValue* v = lookup_var(id->symbol)) return *v;
            // A bare type name used in comptime context.
            if (checker_ && checker_->types.count(id->symbol))
                return ComptimeValue::from_type(id->symbol);
            fail_code("CE003", "'" + id->symbol +
                      "' is not known at compile-time (a runtime value cannot be used "
                      "in a comptime context)");
            return ComptimeValue::none();
        }
        case NodeType::AccessExpression: {
            // Indexing inside comptime bodies: s[i] on strings and arrays,
            // struct[key] for reflection descriptors.
            auto* acc = static_cast<AccessExprNode*>(expr);
            ComptimeValue base = eval(acc->expr.get());
            if (failed_) return ComptimeValue::none();
            ComptimeValue idx = eval(acc->index.get());
            if (failed_) return ComptimeValue::none();

            auto as_index = [&](const ComptimeValue& v, size_t limit, const char* what) -> long long {
                long long i = v.tag == ComptimeValue::Tag::Float ? (long long)v.f_val : v.i_val;
                if (i < 0 || (size_t)i >= limit) {
                    fail(std::string(what) + " index " + std::to_string(i) +
                         " out of range (size " + std::to_string(limit) + ") in comptime context");
                }
                return i;
            };

            if (base.tag == ComptimeValue::Tag::Str) {
                long long i = as_index(idx, base.s_val.size(), "string");
                if (failed_) return ComptimeValue::none();
                return ComptimeValue::from_str(std::string(1, base.s_val[(size_t)i]));
            }
            if (base.tag == ComptimeValue::Tag::Array) {
                long long i = as_index(idx, base.arr_val.size(), "array");
                if (failed_) return ComptimeValue::none();
                return base.arr_val[(size_t)i];
            }
            if (base.tag == ComptimeValue::Tag::Struct) {
                if (idx.tag != ComptimeValue::Tag::Str) {
                    fail("struct index must be a string in comptime context");
                    return ComptimeValue::none();
                }
                auto found = base.struct_val.find(idx.s_val);
                if (found == base.struct_val.end()) {
                    fail("no field '" + idx.s_val + "' in comptime struct");
                    return ComptimeValue::none();
                }
                return found->second;
            }
            fail("unsupported index operation in comptime context");
            return ComptimeValue::none();
        }
        case NodeType::BinaryExpression:
            return eval_binary(static_cast<BinaryExprNode*>(expr));
        case NodeType::LogicalNotExpression: {
            auto* n = static_cast<LogicalNotExprNode*>(expr);
            return ComptimeValue::from_bool(!eval(n->operand.get()).is_truthy());
        }
        case NodeType::UnaryMinusExpression: {
            auto* n = static_cast<UnaryMinusExprNode*>(expr);
            ComptimeValue v = eval(n->operand.get());
            if (v.tag == ComptimeValue::Tag::Int) return ComptimeValue::from_int(-v.i_val);
            if (v.tag == ComptimeValue::Tag::Float) return ComptimeValue::from_float(-v.f_val);
            fail("comptime unary '-' requires a numeric operand");
            return ComptimeValue::none();
        }
        case NodeType::ConditionalExpression: {
            auto* n = static_cast<ConditionalExprNode*>(expr);
            return eval(n->condition.get()).is_truthy()
                       ? eval(n->true_expr.get())
                       : eval(n->false_expr.get());
        }
        case NodeType::RangeExpression:
            return eval_range(static_cast<RangeExprNode*>(expr));
        case NodeType::ArrayExpression: {
            auto* n = static_cast<ArrayExprNode*>(expr);
            std::vector<ComptimeValue> out;
            for (auto& e : n->elements) out.push_back(eval(e.get()));
            return ComptimeValue::from_array(std::move(out));
        }
        case NodeType::VectorExpression: {
            auto* n = static_cast<VectorExprNode*>(expr);
            std::vector<ComptimeValue> out;
            for (auto& e : n->elements) out.push_back(eval(e.get()));
            return ComptimeValue::from_array(std::move(out));
        }
        case NodeType::CallExpression:
            return eval_call(static_cast<CallExprNode*>(expr));
        case NodeType::BuiltinCall:
            return eval_builtin(static_cast<BuiltinCallNode*>(expr));
        case NodeType::MacroCall:
            return eval_macro(static_cast<MacroCallNode*>(expr));
        case NodeType::MemberExpression:
            return eval_member(static_cast<MemberExprNode*>(expr));
        case NodeType::ComptimeExpr:
            return eval(static_cast<ComptimeExprNode*>(expr)->inner.get());
        case NodeType::TypeReflectExpr:
            return eval_type_reflect(static_cast<TypeReflectExprNode*>(expr));
        default:
            fail("unsupported expression in comptime context (node kind " +
                 std::to_string(static_cast<int>(expr->kind)) + ")");
            return ComptimeValue::none();
    }
}

ComptimeValue ComptimeEvaluator::eval_block(const CodeBlock& body) {
    for (const auto& stmt_ptr : body) {
        Stmt* stmt = stmt_ptr.get();
        if (!stmt) continue;
        switch (stmt->kind) {
            case NodeType::ReturnStatement: {
                auto* ret = static_cast<ReturnStmtNode*>(stmt);
                return ret->value ? eval(ret->value.get()) : ComptimeValue::void_value();
            }
            case NodeType::DeclarationStatement: {
                auto* decl = static_cast<DeclarationStmtNode*>(stmt);
                if (decl->target && decl->target->kind == NodeType::Identifier) {
                    auto* id = static_cast<IdentifierNode*>(decl->target.get());
                    declare_var(id->symbol,
                                decl->value ? eval(decl->value.get()) : ComptimeValue::none());
                }
                break;
            }
            case NodeType::AssignmentExpression: {
                auto* as = static_cast<AssignmentExprNode*>(stmt);
                if (as->target && as->target->kind == NodeType::Identifier) {
                    auto* id = static_cast<IdentifierNode*>(as->target.get());
                    ComptimeValue rhs = eval(as->value.get());
                    if (as->op != "=" && !as->op.empty()) {
                        fail("compound assignment '" + as->op + "' is not supported in comptime context");
                        return ComptimeValue::none();
                    }
                    set_var(id->symbol, rhs);
                }
                break;
            }
            case NodeType::DecoratorStatement: {
                // `@compileError("...")` is parsed as a decorator statement, not a
                // builtin call; inside a comptime body it aborts the evaluation.
                auto* dec = static_cast<DecoratorStmtNode*>(stmt);
                if (!dec->entries.empty() && dec->entries[0].name == "compileError") {
                    std::string msg = dec->entries[0].args.empty()
                                      ? std::string() : dec->entries[0].args[0].value;
                    fail("@compileError: " + msg);
                    return ComptimeValue::none();
                }
                if (!dec->entries.empty()) {
                    fail("@" + dec->entries[0].name + " is not supported in comptime context");
                    return ComptimeValue::none();
                }
                break;
            }
            case NodeType::ComptimeDecl: {
                auto* cd = static_cast<ComptimeDeclNode*>(stmt);
                set_var(cd->name, eval(cd->value.get()));
                break;
            }
            case NodeType::IfStatement: {
                auto* ifs = static_cast<IfStatementNode*>(stmt);
                if (eval(ifs->condition.get()).is_truthy()) {
                    ComptimeValue v = eval_block(ifs->consequent);
                    if (failed_ || v.tag != ComptimeValue::Tag::Void) return v;
                } else {
                    ComptimeValue v = eval_block(ifs->alternate);
                    if (failed_ || v.tag != ComptimeValue::Tag::Void) return v;
                }
                break;
            }
            case NodeType::WhileStatement: {
                // `while` inside a comptime function body: evaluated with a
                // bounded iteration count so a bad condition cannot hang the
                // compiler.
                auto* ws = static_cast<WhileStmtNode*>(stmt);
                size_t iterations = 0;
                while (ws->condition && eval(ws->condition.get()).is_truthy()) {
                    if (failed_) return ComptimeValue::none();
                    if (++iterations > MAX_LOOP_ITERATIONS) {
                        fail("comptime while loop exceeded " +
                             std::to_string(MAX_LOOP_ITERATIONS) + " iterations");
                        return ComptimeValue::none();
                    }
                    push_scope();
                    ComptimeValue v = eval_block(ws->body);
                    pop_scope();
                    if (failed_) return ComptimeValue::none();
                    if (v.tag != ComptimeValue::Tag::Void) return v;
                }
                break;
            }
            case NodeType::ComptimeIf: {
                auto* cif = static_cast<ComptimeIfNode*>(stmt);
                ComptimeValue v = eval(cif->condition.get()).is_truthy()
                                      ? eval_block(cif->then_body)
                                      : eval_block(cif->else_body);
                if (failed_ || v.tag != ComptimeValue::Tag::Void) return v;
                break;
            }
            case NodeType::ForStatement: {
                auto* f = static_cast<ForStmtNode*>(stmt);
                ComptimeValue iterable = f->iterable ? eval(f->iterable.get())
                                                     : ComptimeValue::none();
                if (!f->iterable && f->range_start && f->range_end) {
                    RangeExprNode rng(std::unique_ptr<Expr>(static_cast<Expr*>(f->range_start->clone())),
                                      std::unique_ptr<Expr>(static_cast<Expr*>(f->range_end->clone())),
                                      f->range_inclusive);
                    iterable = eval_range(&rng);
                }
                if (iterable.tag != ComptimeValue::Tag::Array) {
                    fail("comptime 'for' requires a range or array");
                    return ComptimeValue::none();
                }
                std::string var;
                if (!f->bindings.empty() && f->bindings[0] &&
                    f->bindings[0]->kind == NodeType::Identifier)
                    var = static_cast<IdentifierNode*>(f->bindings[0].get())->symbol;
                for (auto& elem : iterable.arr_val) {
                    push_scope();
                    if (!var.empty()) set_var(var, elem);
                    ComptimeValue v = eval_block(f->body);
                    pop_scope();
                    if (failed_) return ComptimeValue::none();
                    if (v.tag != ComptimeValue::Tag::Void) return v;
                }
                break;
            }
            case NodeType::ComptimeFor: {
                auto* cf = static_cast<ComptimeForNode*>(stmt);
                ComptimeValue iterable = eval(cf->range.get());
                if (iterable.tag != ComptimeValue::Tag::Array) {
                    fail("comptime 'for' requires a range or array");
                    return ComptimeValue::none();
                }
                for (auto& elem : iterable.arr_val) {
                    push_scope();
                    set_var(cf->var, elem);
                    ComptimeValue v = eval_block(cf->body);
                    pop_scope();
                    if (failed_) return ComptimeValue::none();
                    if (v.tag != ComptimeValue::Tag::Void) return v;
                }
                break;
            }
            case NodeType::ComptimeBlock: {
                auto* cb = static_cast<ComptimeBlockNode*>(stmt);
                push_scope();
                ComptimeValue v = eval_block(cb->body);
                pop_scope();
                if (failed_ || v.tag != ComptimeValue::Tag::Void) return v;
                break;
            }
            case NodeType::ComptimeWhile: {
                auto* cw = static_cast<ComptimeWhileNode*>(stmt);
                size_t guard = 0;
                while (eval(cw->condition.get()).is_truthy()) {
                    if (failed_) return ComptimeValue::none();
                    if (++guard > static_cast<size_t>(MAX_DEPTH) * 1024) {
                        fail("comptime while exceeded iteration limit");
                        return ComptimeValue::none();
                    }
                    push_scope();
                    ComptimeValue v = eval_block(cw->body);
                    pop_scope();
                    if (failed_) return ComptimeValue::none();
                    if (v.tag != ComptimeValue::Tag::Void) return v;
                }
                break;
            }
            case NodeType::FunctionStatement:
            case NodeType::ClassStatement:
            case NodeType::EnumStatement:
            case NodeType::InterfaceStatement:
                break; // declarations: nothing to evaluate
            default:
                if (stmt->position) error_pos_ = std::make_unique<PositionData>(*stmt->position);
        fail("unsupported statement in comptime context (node kind " +
                     std::to_string(static_cast<int>(stmt->kind)) + ")");
                return ComptimeValue::none();
        }
    }
    return ComptimeValue::void_value();
}

// ── ComptimeValue -> literal AST ───────────────────────────────────────────

std::unique_ptr<Expr> ComptimeEvaluator::to_literal(const ComptimeValue& val, const PositionData* pos) {
    std::unique_ptr<Expr> out;
    switch (val.tag) {
        case ComptimeValue::Tag::Int:
            out = std::make_unique<NumericLiteralNode>(std::to_string(val.i_val));
            break;
        case ComptimeValue::Tag::Float: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.17g", val.f_val);
            std::string s = buf;
            if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
                s.find("inf") == std::string::npos && s.find("nan") == std::string::npos)
                s += ".0";
            out = std::make_unique<NumericLiteralNode>(s);
            break;
        }
        case ComptimeValue::Tag::Bool:
            out = std::make_unique<BooleanLiteralNode>(val.b_val);
            break;
        case ComptimeValue::Tag::Str:
            out = std::make_unique<StringLiteralNode>(val.s_val);
            break;
        case ComptimeValue::Tag::Array: {
            // Emit a VectorExprNode, not an ArrayExprNode: that is what the
            // parser produces for a source-level `[...]` literal, and the
            // vector path is the one the checker/codegen index correctly
            // (an ArrayExprNode here boxed as an opaque object and crashed).
            std::vector<std::unique_ptr<Expr>> elements;
            for (const auto& e : val.arr_val) elements.push_back(to_literal(e, pos));
            out = std::make_unique<VectorExprNode>(std::move(elements));
            break;
        }
        default:
            fail("comptime value of kind " + std::to_string(static_cast<int>(val.tag)) +
                 " cannot be materialised as a runtime literal");
            out = std::make_unique<NoneLiteralNode>();
            break;
    }
    if (pos) out->position = std::make_unique<PositionData>(*pos);
    return out;
}

std::unique_ptr<Stmt> ComptimeEvaluator::make_const_decl(const std::string& name,
                                                         const ComptimeValue& val,
                                                         const PositionData* pos) {
    auto target = std::make_unique<IdentifierNode>(name);
    if (pos) target->position = std::make_unique<PositionData>(*pos);
    auto decl = std::make_unique<DeclarationStmtNode>(
        std::move(target), to_literal(val, pos), "automatic", false);
    if (pos) decl->position = std::make_unique<PositionData>(*pos);
    return decl;
}

// ── AST expansion ──────────────────────────────────────────────────────────

std::vector<std::unique_ptr<Stmt>> ComptimeEvaluator::expand_for(ComptimeForNode* node) {
    std::vector<std::unique_ptr<Stmt>> out;
    if (!node) return out;

    ComptimeValue iterable = eval(node->range.get());
    if (failed_) return out;
    if (iterable.tag != ComptimeValue::Tag::Array) {
        fail("comptime 'for' requires a range or array");
        return out;
    }

    const size_t max_unroll = 100000;
    if (iterable.arr_val.size() > max_unroll) {
        fail("comptime unrolling too large (" + std::to_string(iterable.arr_val.size()) +
             " iterations)");
        return out;
    }

    for (const auto& elem : iterable.arr_val) {
        push_scope();
        set_var(node->var, elem);
        out.push_back(make_const_decl(node->var, elem, node->position.get()));

        CodeBlock body_clone;
        body_clone.reserve(node->body.size());
        for (const auto& stmt : node->body)
            if (stmt) body_clone.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));

        CodeBlock expanded = expand_body(std::move(body_clone));
        pop_scope();

        for (auto& s : expanded) out.push_back(std::move(s));
        if (failed_) break;
    }
    return out;
}

std::vector<std::unique_ptr<Stmt>> ComptimeEvaluator::expand_if(ComptimeIfNode* node) {
    std::vector<std::unique_ptr<Stmt>> out;
    if (!node) return out;

    bool taken = eval(node->condition.get()).is_truthy();
    if (failed_) return out;

    CodeBlock chosen;
    const CodeBlock& src = taken ? node->then_body : node->else_body;
    chosen.reserve(src.size());
    for (const auto& stmt : src)
        if (stmt) chosen.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));

    CodeBlock expanded = expand_body(std::move(chosen));
    for (auto& s : expanded) out.push_back(std::move(s));
    return out;
}

std::vector<std::unique_ptr<Stmt>> ComptimeEvaluator::expand_while(ComptimeWhileNode* node) {
    std::vector<std::unique_ptr<Stmt>> out;
    if (!node) return out;

    size_t guard = 0;
    while (eval(node->condition.get()).is_truthy()) {
        if (failed_) return out;
        if (++guard > 100000) {
            fail("comptime 'while' unrolled too many times (>100000)");
            return out;
        }
        // Assignments to comptime variables run at compile time (Zig's
        // `inline while` counter pattern); everything else is emitted.
        for (const auto& stmt : node->body) {
            if (!stmt) continue;
            if (stmt->kind == NodeType::AssignmentExpression) {
                auto* as = static_cast<AssignmentExprNode*>(stmt.get());
                if (as->target && as->target->kind == NodeType::Identifier) {
                    auto* id = static_cast<IdentifierNode*>(as->target.get());
                    if (lookup_var(id->symbol)) {
                        ComptimeValue v = eval(as->value.get());
                        if (failed_) return out;
                        set_var(id->symbol, v);
                        out.push_back(make_const_decl(id->symbol, v, stmt->position.get()));
                        continue;
                    }
                }
            }
            CodeBlock one;
            one.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
            CodeBlock expanded = expand_body(std::move(one));
            for (auto& s : expanded) out.push_back(std::move(s));
            if (failed_) return out;
        }
    }
    return out;
}

CodeBlock ComptimeEvaluator::expand_body(CodeBlock body) {
    CodeBlock out;
    out.reserve(body.size());

    for (auto& stmt_ptr : body) {
        Stmt* stmt = stmt_ptr.get();
        if (!stmt) continue;

        switch (stmt->kind) {
            case NodeType::ComptimeFuncDef: {
                register_func(static_cast<ComptimeFuncNode*>(stmt));
                continue; // compile-time only: dropped from the AST
            }
            case NodeType::ComptimeDecl: {
                auto* d = static_cast<ComptimeDeclNode*>(stmt);
                ComptimeValue v = eval(d->value.get());
                if (failed_) return out;
                set_var(d->name, v);
                out.push_back(make_const_decl(d->name, v, d->position.get()));
                continue;
            }
            case NodeType::ComptimeFor: {
                auto expanded = expand_for(static_cast<ComptimeForNode*>(stmt));
                if (failed_) return out;
                for (auto& s : expanded) out.push_back(std::move(s));
                continue;
            }
            case NodeType::ComptimeIf: {
                auto expanded = expand_if(static_cast<ComptimeIfNode*>(stmt));
                if (failed_) return out;
                for (auto& s : expanded) out.push_back(std::move(s));
                continue;
            }
            case NodeType::ComptimeBlock: {
                auto* b = static_cast<ComptimeBlockNode*>(stmt);
                push_scope();
                for (auto& inner : b->body) {
                    if (!inner) continue;
                    if (inner->kind == NodeType::AssignmentExpression) {
                        auto* as = static_cast<AssignmentExprNode*>(inner.get());
                        if (as->target && as->target->kind == NodeType::Identifier) {
                            auto* id = static_cast<IdentifierNode*>(as->target.get());
                            ComptimeValue v = eval(as->value.get());
                            if (failed_) { pop_scope(); return out; }
                            set_var(id->symbol, v);
                            out.push_back(make_const_decl(id->symbol, v, inner->position.get()));
                            continue;
                        }
                    }
                    CodeBlock one;
                    one.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(inner->clone())));
                    CodeBlock expanded = expand_body(std::move(one));
                    for (auto& s : expanded) out.push_back(std::move(s));
                    if (failed_) { pop_scope(); return out; }
                }
                pop_scope();
                continue;
            }
            case NodeType::ComptimeExpr: {
                // Statement-level `comptime <expr>`: evaluated for its effect only.
                eval(static_cast<ComptimeExprNode*>(stmt)->inner.get());
                if (failed_) return out;
                continue;
            }
            case NodeType::MacroCall: {
                // Statement-level `name! { ... }`: evaluated for its effect only.
                eval_macro(static_cast<MacroCallNode*>(stmt));
                if (failed_) return out;
                continue;
            }
            case NodeType::ComptimeWhile: {
                auto expanded = expand_while(static_cast<ComptimeWhileNode*>(stmt));
                if (failed_) return out;
                for (auto& s : expanded) out.push_back(std::move(s));
                continue;
            }
            case NodeType::FunctionStatement: {
                auto* fn = static_cast<FunctionStmtNode*>(stmt);
                std::vector<bool> flags;
                flags.reserve(fn->parameters.size());
                for (const auto& p : fn->parameters) flags.push_back(p.is_comptime);
                register_func_signature(fn->name, std::move(flags));
                fn->body = expand_body(std::move(fn->body));
                if (failed_) return out;
                break;
            }
            case NodeType::IfStatement: {
                auto* ifs = static_cast<IfStatementNode*>(stmt);
                ifs->consequent = expand_body(std::move(ifs->consequent));
                if (failed_) return out;
                ifs->alternate = expand_body(std::move(ifs->alternate));
                if (failed_) return out;
                break;
            }
            case NodeType::ForStatement: {
                auto* f = static_cast<ForStmtNode*>(stmt);
                f->body = expand_body(std::move(f->body));
                if (failed_) return out;
                f->else_block = expand_body(std::move(f->else_block));
                if (failed_) return out;
                break;
            }
            case NodeType::WhileStatement: {
                auto* w = static_cast<WhileStmtNode*>(stmt);
                w->body = expand_body(std::move(w->body));
                if (failed_) return out;
                break;
            }
            case NodeType::DecoratorStatement:
                // Interpreted by eval_block inside comptime bodies (e.g.
                // @compileError); nothing to expand here.
                break;
            case NodeType::ForeverStatement: {
                auto* f = static_cast<ForeverStmtNode*>(stmt);
                f->body = expand_body(std::move(f->body));
                if (failed_) return out;
                break;
            }
            case NodeType::MatchStatement: {
                auto* m = static_cast<MatchStmtNode*>(stmt);
                for (auto& b : m->bodies) {
                    b = expand_body(std::move(b));
                    if (failed_) return out;
                }
                break;
            }
            case NodeType::ClassStatement: {
                auto* c = static_cast<ClassStmtNode*>(stmt);
                for (auto& method : c->methods) {
                    if (!method || !method->method_def) continue;
                    if (method->method_def->kind == NodeType::FunctionStatement) {
                        auto* fn = static_cast<FunctionStmtNode*>(method->method_def.get());
                        fn->body = expand_body(std::move(fn->body));
                        if (failed_) return out;
                    }
                }
                break;
            }
            case NodeType::OrExpression: {
                auto* oe = static_cast<OrExprNode*>(stmt);
                if (oe->is_block_handler) {
                    oe->block_stmts = expand_body(std::move(oe->block_stmts));
                    if (failed_) return out;
                }
                break;
            }
            default:
                break;
        }

        // Fold inline `comptime <expr>` / @builtins inside ordinary statements.
        rewrite_stmt(stmt_ptr.get());
        if (failed_) return out;

        out.push_back(std::move(stmt_ptr));
    }
    return out;
}

// ── Inline folding (Zig-style: `comptime` and @builtins usable anywhere) ───

void ComptimeEvaluator::rewrite_expr(std::unique_ptr<Expr>& slot) {
    rewrite_expr_impl(&slot, slot.get());
}

// Compile-time-only expressions that may appear as the object of a member access
// inside runtime code: `type.ast(f).name`, `@fields(T).x`, `comptime(expr).x`.
static bool is_comptime_object(const Expr* e) {
    if (!e) return false;
    if (e->kind == NodeType::BuiltinCall || e->kind == NodeType::TypeReflectExpr ||
        e->kind == NodeType::ComptimeExpr)
        return true;
    if (e->kind != NodeType::CallExpression) return false;
    auto* c = static_cast<const CallExprNode*>(e);
    if (!c->caller || c->caller->kind != NodeType::MemberExpression) return false;
    auto* m = static_cast<const MemberExprNode*>(c->caller.get());
    return m->object && m->object->kind == NodeType::Identifier &&
           static_cast<const IdentifierNode*>(m->object.get())->symbol == "type";
}

void ComptimeEvaluator::rewrite_expr_impl(std::unique_ptr<Expr>* slot, Expr* e) {
    if (!e) return;
    switch (e->kind) {
        case NodeType::ComptimeExpr: {
            ComptimeValue v = eval(static_cast<ComptimeExprNode*>(e)->inner.get());
            if (failed_) return;
            if (slot) *slot = to_literal(v, e->position.get());
            return;
        }
        case NodeType::BuiltinCall: {
            ComptimeValue v = eval_builtin(static_cast<BuiltinCallNode*>(e));
            if (failed_) return;
            if (slot) *slot = to_literal(v, e->position.get());
            return;
        }
        case NodeType::TypeReflectExpr: {
            ComptimeValue v = eval_type_reflect(static_cast<TypeReflectExprNode*>(e));
            if (failed_) return;
            if (slot) *slot = to_literal(v, e->position.get());
            return;
        }
        case NodeType::MacroCall: {
            ComptimeValue v = eval_macro(static_cast<MacroCallNode*>(e));
            if (failed_) return;
            if (slot) *slot = to_literal(v, e->position.get());
            return;
        }
        case NodeType::BinaryExpression: {
            auto* n = static_cast<BinaryExprNode*>(e);
            rewrite_expr(n->left);
            rewrite_expr(n->right);
            return;
        }
        case NodeType::LogicalNotExpression:
            rewrite_expr(static_cast<LogicalNotExprNode*>(e)->operand);
            return;
        case NodeType::UnaryMinusExpression:
            rewrite_expr(static_cast<UnaryMinusExprNode*>(e)->operand);
            return;
        case NodeType::ConditionalExpression: {
            auto* n = static_cast<ConditionalExprNode*>(e);
            rewrite_expr(n->condition);
            rewrite_expr(n->true_expr);
            rewrite_expr(n->false_expr);
            return;
        }
        case NodeType::RangeExpression: {
            auto* n = static_cast<RangeExprNode*>(e);
            rewrite_expr(n->start);
            rewrite_expr(n->end);
            return;
        }
        case NodeType::ArrayExpression: {
            auto* n = static_cast<ArrayExprNode*>(e);
            for (auto& el : n->elements) rewrite_expr(el);
            return;
        }
        case NodeType::VectorExpression: {
            auto* n = static_cast<VectorExprNode*>(e);
            for (auto& el : n->elements) rewrite_expr(el);
            return;
        }
        case NodeType::TupleExpression: {
            auto* n = static_cast<TupleExprNode*>(e);
            for (auto& el : n->elements) rewrite_expr(el);
            return;
        }
        case NodeType::AccessExpression: {
            auto* n = static_cast<AccessExprNode*>(e);
            rewrite_expr(n->expr);
            rewrite_expr(n->index);
            return;
        }
        case NodeType::MemberExpression: {
            auto* n = static_cast<MemberExprNode*>(e);
            // Fold a reflection field used as a value. Only scalars are folded:
            // arrays and nested structs are consumed by `comptime for`, which
            // evaluates the iterable itself.
            if (slot && is_comptime_object(n->object.get())) {
                ComptimeValue v = eval(n);
                if (failed_) return;
                if (v.tag == ComptimeValue::Tag::Str || v.tag == ComptimeValue::Tag::Int ||
                    v.tag == ComptimeValue::Tag::Float || v.tag == ComptimeValue::Tag::Bool) {
                    *slot = to_literal(v, e->position.get());
                    return;
                }
            }
            rewrite_expr(n->object);
            rewrite_expr(n->property);
            return;
        }
        case NodeType::CallExpression: {
            auto* n = static_cast<CallExprNode*>(e);
            rewrite_expr(n->caller);

            // `comptime N: T` parameters: the argument must be known at compile
            // time, so fold it to a literal here (there is no monomorphisation
            // yet — the callee is still emitted once).
            std::vector<bool> comptime_flags;
            if (n->caller && n->caller->kind == NodeType::Identifier) {
                auto it = func_comptime_params_.find(
                    static_cast<IdentifierNode*>(n->caller.get())->symbol);
                if (it != func_comptime_params_.end()) comptime_flags = it->second;
            }

            for (size_t i = 0; i < n->args.size(); ++i) {
                auto& a = n->args[i];
                if (!a) continue;
                if (i < comptime_flags.size() && comptime_flags[i] && a->value) {
                    std::string fname =
                        static_cast<IdentifierNode*>(n->caller.get())->symbol;
                    ComptimeValue v = eval(a->value.get());
                    if (failed_) {
                        error_ = "argument " + std::to_string(i + 1) + " of '" + fname +
                                 "' must be a compile-time constant (declared `comptime`)";
                        return;
                    }
                    a->value = to_literal(v, a->value->position.get());
                    if (failed_) return;
                    continue;
                }
                rewrite_expr(a->value);
            }
            return;
        }
        case NodeType::AssignmentExpression: {
            auto* n = static_cast<AssignmentExprNode*>(e);
            rewrite_expr(n->target);
            rewrite_expr(n->value);
            return;
        }
        case NodeType::SliceExpression: {
            auto* n = static_cast<SliceExprNode*>(e);
            rewrite_expr(n->collection);
            rewrite_expr(n->start);
            rewrite_expr(n->stop);
            rewrite_expr(n->step);
            return;
        }
        case NodeType::NewExpression: {
            auto* n = static_cast<NewExprNode*>(e);
            for (auto& a : n->arguments) rewrite_expr(a);
            return;
        }
        case NodeType::ClosureExpression: {
            auto* n = static_cast<ClosureExprNode*>(e);
            for (auto& st : n->body) rewrite_stmt(st.get());
            return;
        }
        default:
            return; // nodes without (walked) sub-expressions
    }
}

void ComptimeEvaluator::rewrite_stmt(Stmt* stmt) {
    if (!stmt) return;
    switch (stmt->kind) {
        case NodeType::DeclarationStatement: {
            auto* d = static_cast<DeclarationStmtNode*>(stmt);
            rewrite_expr(d->value);
            return;
        }
        case NodeType::AssignmentExpression: {
            auto* a = static_cast<AssignmentExprNode*>(stmt);
            rewrite_expr(a->target);
            rewrite_expr(a->value);
            return;
        }
        case NodeType::ReturnStatement:
            rewrite_expr(static_cast<ReturnStmtNode*>(stmt)->value);
            return;
        case NodeType::IfStatement:
            rewrite_expr(static_cast<IfStatementNode*>(stmt)->condition);
            return;
        case NodeType::WhileStatement:
            rewrite_expr(static_cast<WhileStmtNode*>(stmt)->condition);
            return;
        case NodeType::ForStatement: {
            auto* f = static_cast<ForStmtNode*>(stmt);
            rewrite_expr(f->range_start);
            rewrite_expr(f->range_end);
            rewrite_expr(f->iterable);
            for (auto& b : f->bindings) rewrite_expr(b);
            return;
        }
        case NodeType::ComptimeDecl:
            rewrite_expr(static_cast<ComptimeDeclNode*>(stmt)->value);
            return;
        case NodeType::ComptimeFor:
            rewrite_expr(static_cast<ComptimeForNode*>(stmt)->range);
            return;
        case NodeType::ComptimeIf:
            rewrite_expr(static_cast<ComptimeIfNode*>(stmt)->condition);
            return;
        case NodeType::ComptimeWhile:
            rewrite_expr(static_cast<ComptimeWhileNode*>(stmt)->condition);
            return;
        case NodeType::ComptimeBlock: {
            auto* b = static_cast<ComptimeBlockNode*>(stmt);
            for (auto& inner : b->body) rewrite_stmt(inner.get());
            return;
        }
        case NodeType::ThrowStatement: {
            auto* t = static_cast<ThrowStatementNode*>(stmt);
            if (t->exception && is_expr_kind(t->exception->kind))
                rewrite_expr_impl(nullptr, static_cast<Expr*>(t->exception.get()));
            return;
        }
        default:
            if (is_expr_kind(stmt->kind))
                rewrite_expr_impl(nullptr, static_cast<Expr*>(stmt));
            return;
    }
}

} // namespace nv
