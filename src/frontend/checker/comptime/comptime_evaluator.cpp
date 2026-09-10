#include "frontend/comptime/comptime_evaluator.hpp"
#include "frontend/ast/program.hpp"
#include "frontend/checker/checker.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

namespace nv {

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
    if (!failed_) {
        failed_ = true;
        error_ = message;
    }
}

void ComptimeEvaluator::push_scope() { scope_stack_.emplace_back(); }
void ComptimeEvaluator::pop_scope()  { if (scope_stack_.size() > 1) scope_stack_.pop_back(); }

void ComptimeEvaluator::set_var(const std::string& name, const ComptimeValue& val) {
    if (scope_stack_.empty()) scope_stack_.emplace_back();
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
    ComptimeValue l = eval(node->left.get());
    ComptimeValue r = eval(node->right.get());
    const std::string& op = node->op;

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
    // <struct_value>.<field>
    if (node->object && node->object->kind == NodeType::Identifier &&
        node->property && node->property->kind == NodeType::Identifier) {
        auto* obj_id = static_cast<IdentifierNode*>(node->object.get());
        auto* prop_id = static_cast<IdentifierNode*>(node->property.get());
        ComptimeValue* v = lookup_var(obj_id->symbol);
        if (v && v->tag == ComptimeValue::Tag::Struct) {
            auto found = v->struct_val.find(prop_id->symbol);
            if (found != v->struct_val.end()) return found->second;
            fail("comptime struct has no field '" + prop_id->symbol + "'");
            return ComptimeValue::none();
        }
    }
    fail("unsupported comptime member access");
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

ComptimeValue ComptimeEvaluator::eval_call(CallExprNode* node) {
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
        if (i < arg_vals.size()) set_var(pname, arg_vals[i]);
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
            fail("unknown comptime identifier '" + id->symbol + "'");
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
                    set_var(id->symbol, decl->value ? eval(decl->value.get()) : ComptimeValue::none());
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
            std::vector<std::unique_ptr<Expr>> elements;
            for (const auto& e : val.arr_val) elements.push_back(to_literal(e, pos));
            out = std::make_unique<ArrayExprNode>(std::move(elements));
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
            case NodeType::ComptimeWhile: {
                auto expanded = expand_while(static_cast<ComptimeWhileNode*>(stmt));
                if (failed_) return out;
                for (auto& s : expanded) out.push_back(std::move(s));
                continue;
            }
            case NodeType::FunctionStatement: {
                auto* fn = static_cast<FunctionStmtNode*>(stmt);
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
            rewrite_expr(n->object);
            rewrite_expr(n->property);
            return;
        }
        case NodeType::CallExpression: {
            auto* n = static_cast<CallExprNode*>(e);
            rewrite_expr(n->caller);
            for (auto& a : n->args)
                if (a) rewrite_expr(a->value);
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
