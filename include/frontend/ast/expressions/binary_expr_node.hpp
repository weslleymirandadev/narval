#pragma once
#include "../types.hpp"
#include <string>
#include <memory>

class BinaryExprNode : public Expr {
public:
    std::string op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;

    BinaryExprNode(std::string operator_, std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs)
        : Expr(NodeType::BinaryExpression), op(std::move(operator_)), left(std::move(lhs)), right(std::move(rhs)) {}

    ~BinaryExprNode() override = default;

    Node* clone() const override {
        auto cloned_left = left ? std::unique_ptr<Expr>(static_cast<Expr*>(left->clone())) : nullptr;
        auto cloned_right = right ? std::unique_ptr<Expr>(static_cast<Expr*>(right->clone())) : nullptr;
        auto* node = new BinaryExprNode(op, std::move(cloned_left), std::move(cloned_right));
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};

