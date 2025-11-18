#pragma once
#include "../types.hpp"
#include <memory>

class UnaryMinusExprNode : public Expr {
public:
    std::unique_ptr<Expr> operand;

    UnaryMinusExprNode(std::unique_ptr<Expr> operand)
        : Expr(NodeType::UnaryMinusExpression), operand(std::move(operand)) {}

    ~UnaryMinusExprNode() override = default;

    Node* clone() const override {
        auto cloned_operand = operand ? std::unique_ptr<Expr>(static_cast<Expr*>(operand->clone())) : nullptr;
        auto* node = new UnaryMinusExprNode(std::move(cloned_operand));
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

