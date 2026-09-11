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

    // ── User-defined @derive ────────────────────────────────────────────────
    // A user derive is a top-level `comptime def` named `derive_<name>`. The
    // @derive pass runs BEFORE the expansion pass that would register it, so the
    // pass calls register_user_derives() on the module body first and then
    // evaluates the derive with call_comptime_func().
    void register_user_derives(const CodeBlock& body);
    bool has_comptime_func(const std::string& name) const {
        return comptime_funcs_.count(name) != 0;
    }
    // Evaluates a registered comptime function with the given arguments. Returns
    // false and fills `error` when the function is unknown or the evaluation fails.
    bool call_comptime_func(const std::string& name,
                            const std::vector<ComptimeValue>& args,
                            ComptimeValue& out, std::string& error);
    // Compile-time diagnostic code for the recorded failure (COMPTIME_SPEC 7):
    // CE001 evaluation failure, CE003 non-comptime value in comptime context.
    std::string error_code() const { return error_code_; }
    // Position of the innermost expression being evaluated when the failure was
    // recorded (spec 7 wants the diagnostic on the offending expression, not on
    // the whole program). A COPY is stored: the expansion may replace or free
    // the node while still reporting.
    const PositionData* error_position() const { return error_pos_.get(); }

private:
    static constexpr int MAX_DEPTH = 1024;

    Checker* checker_;
    int call_depth_ = 0;
    bool failed_ = false;
    std::string error_;
    std::string error_code_ = "CE001";
    std::unique_ptr<PositionData> error_pos_;

    std::vector<std::unordered_map<std::string, ComptimeValue>> scope_stack_;
    // Index of the scope belonging to the comptime function/macro being
    // evaluated. Assignments must not reach scopes above it: a callee reusing a
    // name like `i` or `out` used to overwrite the caller's variable.
    size_t func_scope_base_ = 0;
    std::unordered_map<std::string, ComptimeFuncNode*> comptime_funcs_;
    // function name → per-parameter `comptime` flags
    std::unordered_map<std::string, std::vector<bool>> func_comptime_params_;

    void fail(const std::string& message);
    // Records a failure with an explicit diagnostic code (spec 7).
    void fail_code(const std::string& code, const std::string& message);
    void push_scope();
    void pop_scope();
    void set_var(const std::string& name, const ComptimeValue& val);
    // Binds a NEW name in the innermost scope (parameters, declarations);
    // set_var() instead updates the binding where it already lives.
    void declare_var(const std::string& name, const ComptimeValue& val);
    ComptimeValue* lookup_var(const std::string& name);

    ComptimeValue eval_binary(BinaryExprNode* node);
    ComptimeValue eval_call(CallExprNode* node);
    // Body of a comptime function call: binds the arguments positionally in a new
    // scope and evaluates the body. Shared by eval_call() and the user @derive path.
    ComptimeValue call_func(ComptimeFuncNode* fn, const std::vector<ComptimeValue>& arg_vals);
    ComptimeValue eval_builtin(BuiltinCallNode* node);
    ComptimeValue eval_macro(MacroCallNode* node);
    ComptimeValue eval_member(MemberExprNode* node);
    ComptimeValue eval_range(RangeExprNode* node);
    std::unique_ptr<Stmt> make_const_decl(const std::string& name, const ComptimeValue& val,
                                          const PositionData* pos);

    // Source queued by `@emit("<narval source>")` while a comptime body runs.
    // expand_body parses it and splices the resulting statements in right after
    // the statement that emitted them, so a comptime macro can generate real
    // `def`s (COMPTIME_SPEC 5.11: parser generators). The queue is shared by the
    // whole expansion, so emission works from any nesting depth.
    std::vector<std::string> emitted_sources_;
    size_t emitted_stmts_ = 0;
    // Parses and appends every queued source (clears the queue). Enforces a cap so
    // a macro that keeps emitting can not spin the compiler forever.
    void drain_emitted(std::vector<std::unique_ptr<Stmt>>& out);
};

} // namespace nv
