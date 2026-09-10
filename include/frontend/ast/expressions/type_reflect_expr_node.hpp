#pragma once
#include "../types.hpp"
#include <string>

// type.fields(T) / type.name(T) / type.kind(T) / type.has_field(T, "f")
// Only valid in comptime context — resolved by the ComptimeEvaluator.
class TypeReflectExprNode : public Expr {
public:
    std::string fn;          // "fields", "methods", "name", "kind", "has_field", "param_count"
    std::string target_type; // inspected type name
    std::string extra_arg;   // has_field: field name

    TypeReflectExprNode(std::string fn, std::string target_type, std::string extra_arg = "")
        : Expr(NodeType::TypeReflectExpr),
          fn(std::move(fn)), target_type(std::move(target_type)), extra_arg(std::move(extra_arg)) {}

    ~TypeReflectExprNode() override = default;

    Node* clone() const override {
        auto* node = new TypeReflectExprNode(fn, target_type, extra_arg);
        if (position) node->position = std::make_unique<PositionData>(*position);
        return node;
    }
};
