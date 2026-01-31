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
        auto* node = new LogicalNotExprNode(std::move(cloned_operand));
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};

