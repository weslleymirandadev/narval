#include "frontend/checker/expressions/check_assignment_expr.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type>& check_assignment_expr(nv::Checker* ch, Node* node) {
    static thread_local std::shared_ptr<nv::Type> result;
    const auto* assign = static_cast<AssignmentExprNode*>(node);
    // A `comptime` binding is a constant: the value was folded at compile time, and the
    // assignment used to be accepted, silently turning the name into a runtime variable.
    // The criterion is the scope's `comptime` mark (see Namespace::mark_comptime) — the
    // scope's immutability flag is not usable here, because every ordinary declaration
    // lands in it as immutable too (using it refused `x = 5; x = 6;`).
    if (assign->target && assign->target->kind == NodeType::Identifier &&
        ch->scope->is_comptime(
            static_cast<IdentifierNode*>(assign->target.get())->symbol)) {
        const std::string& sym = static_cast<IdentifierNode*>(assign->target.get())->symbol;
        ch->error(node, "Cannot assign to '" + sym + "'; a `comptime` binding is a constant");
        result = ch->gettyptr("None");
        return result;
    }

    // The target of an assignment is inferred like any expression, and for `obj.field` that
    // goes through the class branch of check_member_expr, which needs to know it is looking
    // at an lvalue: writing a field has rules reading it does not.
    const bool was_target = ch->inferring_assignment_target;
    ch->inferring_assignment_target = true;
    auto left_type = ch->infer_expr(assign->target.get());
    ch->inferring_assignment_target = was_target;
    auto right_type = ch->infer_expr(assign->value.get());

    left_type = ch->unify_ctx.resolve(left_type);
    right_type = ch->unify_ctx.resolve(right_type);

    // A function typed None returns no value, so there is nothing to assign.
    // This used to compile and then fail at lowering ("Call parameter type does
    // not match function signature! call void @g()"), which named the generated
    // call instead of the source line.
    if (assign->value && assign->value->kind == NodeType::CallExpression &&
        right_type && right_type->kind == nv::Kind::NONE) {
        ch->error(node, "The result of a function that returns None cannot be assigned");
        result = ch->gettyptr("None");
        return result;
    }

    bool left_is_int = left_type->kind == nv::Kind::INT;
    bool left_is_float = left_type->kind == nv::Kind::FLOAT;
    bool right_is_int = right_type->kind == nv::Kind::INT;
    bool right_is_float = right_type->kind == nv::Kind::FLOAT;

    if (left_is_int && right_is_float) {
        left_type = ch->gettyptr("float");
    } else if (left_is_float && right_is_int) {
        right_type = ch->gettyptr("float");
    }

    try {
        ch->unify_ctx.unify(left_type, right_type);
    } catch (std::runtime_error& e) {
        ch->error(node, "Assignment type error: " + std::string(e.what()));
        result = ch->gettyptr("None");
        return result;
    }

    result = ch->unify_ctx.resolve(right_type);
    return result;
}
