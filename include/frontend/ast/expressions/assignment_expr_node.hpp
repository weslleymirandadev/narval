#pragma once
#include "../types.hpp"
#include <memory>
#include <string>

class AssignmentExprNode : public Expr {
public:
    std::unique_ptr<Expr> target;
    std::string op;
    std::unique_ptr<Expr> value;

    AssignmentExprNode(std::unique_ptr<Expr> tgt, std::string opr, std::unique_ptr<Expr> val)
        : Expr(NodeType::AssignmentExpression), target(std::move(tgt)), op(std::move(opr)), value(std::move(val)) {}

    ~AssignmentExprNode() override = default;

    Node* clone() const override {
        auto cloned_target = target ? std::unique_ptr<Expr>(static_cast<Expr*>(target->clone())) : nullptr;
        auto cloned_value = value ? std::unique_ptr<Expr>(static_cast<Expr*>(value->clone())) : nullptr;
        auto* node = new AssignmentExprNode(std::move(cloned_target), op, std::move(cloned_value));
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

