#pragma once
#include "../types.hpp"
#include <memory>

class DecrementExprNode : public Expr {
public:
    std::unique_ptr<Expr> operand;

    DecrementExprNode(std::unique_ptr<Expr> operand)
        : Expr(NodeType::DecrementExpression), operand(std::move(operand)) {}

    ~DecrementExprNode() override = default;

    Node* clone() const override {
        auto cloned_operand = operand ? std::unique_ptr<Expr>(static_cast<Expr*>(operand->clone())) : nullptr;
        return new DecrementExprNode(std::move(cloned_operand));
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

