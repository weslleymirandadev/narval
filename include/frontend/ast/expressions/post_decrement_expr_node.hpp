#pragma once
#include "../types.hpp"
#include <memory>

class PostDecrementExprNode : public Expr {
public:
    std::unique_ptr<Expr> operand;

    PostDecrementExprNode(std::unique_ptr<Expr> operand)
        : Expr(NodeType::PostDecrementExpression), operand(std::move(operand)) {}

    ~PostDecrementExprNode() override = default;

    Node* clone() const override {
        auto cloned_operand = operand ? std::unique_ptr<Expr>(static_cast<Expr*>(operand->clone())) : nullptr;
        return new PostDecrementExprNode(std::move(cloned_operand));
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

