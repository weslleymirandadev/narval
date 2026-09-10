#pragma once
#include "frontend/ast/ast.hpp"
#include "frontend/comptime/comptime_value.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace nv {

class Checker;

// Compile-time expression evaluator + AST expander for the CTE feature.
//
// The checker runs an expansion pass before type checking: every `comptime`
// construct is folded into plain AST (literals, unrolled statements), so the
// existing checker/codegen never see a comptime node.
class ComptimeEvaluator {
public:
    explicit ComptimeEvaluator(Checker* checker = nullptr);

    // Expression evaluation in comptime context.
    ComptimeValue eval(Expr* expr);

    // Statement block evaluation; returns the value of the executed `return`.
    ComptimeValue eval_block(const CodeBlock& body);

    // Registers a `comptime def` so later calls can resolve it.
    void register_func(ComptimeFuncNode* node);

    // Records which parameters of a (runtime) function are `comptime N: T`.
    // Call sites must then supply compile-time constants for those positions.
    void register_func_signature(const std::string& name, std::vector<bool> comptime_flags);

    // type.fields(T), type.name(T), type.kind(T), type.has_field(T, "f"), ...
    ComptimeValue eval_type_reflect(TypeReflectExprNode* node);

    // ComptimeValue -> literal AST node.
    std::unique_ptr<Expr> to_literal(const ComptimeValue& val, const PositionData* pos);

    // Expansion entry points.
    std::vector<std::unique_ptr<Stmt>> expand_for(ComptimeForNode* node);
    std::vector<std::unique_ptr<Stmt>> expand_if(ComptimeIfNode* node);
    std::vector<std::unique_ptr<Stmt>> expand_while(ComptimeWhileNode* node);
    // Rewrites a statement list, expanding every comptime construct in place.
    CodeBlock expand_body(CodeBlock body);

    // Recursively folds `comptime <expr>` / @builtins / type reflection that
    // appear inside ordinary expressions (Zig-style: usable anywhere).
    void rewrite_expr(std::unique_ptr<Expr>& slot);
    void rewrite_stmt(Stmt* stmt);
    void rewrite_expr_impl(std::unique_ptr<Expr>* slot, Expr* e);

    bool failed() const { return failed_; }
    const std::string& error_message() const { return error_; }

private:
    static constexpr int MAX_DEPTH = 1024;

    Checker* checker_;
    int call_depth_ = 0;
    bool failed_ = false;
    std::string error_;

    std::vector<std::unordered_map<std::string, ComptimeValue>> scope_stack_;
    std::unordered_map<std::string, ComptimeFuncNode*> comptime_funcs_;
    // function name → per-parameter `comptime` flags
    std::unordered_map<std::string, std::vector<bool>> func_comptime_params_;

    void fail(const std::string& message);
    void push_scope();
    void pop_scope();
    void set_var(const std::string& name, const ComptimeValue& val);
    ComptimeValue* lookup_var(const std::string& name);

    ComptimeValue eval_binary(BinaryExprNode* node);
    ComptimeValue eval_call(CallExprNode* node);
    ComptimeValue eval_builtin(BuiltinCallNode* node);
    ComptimeValue eval_macro(MacroCallNode* node);
    ComptimeValue eval_member(MemberExprNode* node);
    ComptimeValue eval_range(RangeExprNode* node);
    std::unique_ptr<Stmt> make_const_decl(const std::string& name, const ComptimeValue& val,
                                          const PositionData* pos);
};

} // namespace nv
