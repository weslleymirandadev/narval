#pragma once
#include "../types.hpp"
#include <memory>

class LogicalNotExprNode : public Expr {
public:
    std::unique_ptr<Expr> operand;

    LogicalNotExprNode(std::unique_ptr<Expr> operand)
        : Expr(NodeType::LogicalNotExpression), operand(std::move(operand)) {}

    ~LogicalNotExprNode() override = default;

    Node* clone() const override {
        auto cloned_operand = operand ? std::unique_ptr<Expr>(static_cast<Expr*>(operand->clone())) : nullptr;
        return new LogicalNotExprNode(std::move(cloned_operand));
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

