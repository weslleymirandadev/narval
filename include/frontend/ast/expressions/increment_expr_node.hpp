#pragma once
#include "../types.hpp"
#include <memory>

class IncrementExprNode : public Expr {
public:
    std::unique_ptr<Expr> operand;

    IncrementExprNode(std::unique_ptr<Expr> operand)
        : Expr(NodeType::IncrementExpression), operand(std::move(operand)) {}

    ~IncrementExprNode() override = default;

    Node* clone() const override {
        auto cloned_operand = operand ? std::unique_ptr<Expr>(static_cast<Expr*>(operand->clone())) : nullptr;
        auto* node = new IncrementExprNode(std::move(cloned_operand));
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

