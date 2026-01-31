#pragma once
#include "../types.hpp"
#include <memory>

class ConditionalExprNode : public Expr {
public:
    std::unique_ptr<Expr> true_expr;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Expr> false_expr;

    ConditionalExprNode(
        std::unique_ptr<Expr> t,
        std::unique_ptr<Expr> c,
        std::unique_ptr<Expr> f
    ) : Expr(NodeType::ConditionalExpression),
        true_expr(std::move(t)),
        condition(std::move(c)),
        false_expr(std::move(f)) {}

    ~ConditionalExprNode() override = default;

    Node* clone() const override {
        auto* node = new ConditionalExprNode(
            std::unique_ptr<Expr>(static_cast<Expr*>(true_expr->clone())),
            std::unique_ptr<Expr>(static_cast<Expr*>(condition->clone())),
            std::unique_ptr<Expr>(static_cast<Expr*>(false_expr->clone()))
        );
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};
