#pragma once
#include "../types.hpp"
#include <memory>

class PostIncrementExprNode : public Expr {
public:
    std::unique_ptr<Expr> operand;

    PostIncrementExprNode(std::unique_ptr<Expr> operand)
        : Expr(NodeType::PostIncrementExpression), operand(std::move(operand)) {}

    ~PostIncrementExprNode() override = default;

    Node* clone() const override {
        auto cloned_operand = operand ? std::unique_ptr<Expr>(static_cast<Expr*>(operand->clone())) : nullptr;
        return new PostIncrementExprNode(std::move(cloned_operand));
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

