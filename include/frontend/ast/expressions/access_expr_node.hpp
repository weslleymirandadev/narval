#pragma once
#include "../types.hpp"
#include <memory>

class AccessExprNode : public Expr {
public:
    std::unique_ptr<Expr> expr;
    std::unique_ptr<Expr> index;

    AccessExprNode(std::unique_ptr<Expr> expr, std::unique_ptr<Expr> index)
        : Expr(NodeType::AccessExpression), expr(std::move(expr)), index(std::move(index)) {}

    ~AccessExprNode() override = default;

    Node* clone() const override {
        auto cloned_expr = expr ? std::unique_ptr<Expr>(static_cast<Expr*>(expr->clone())) : nullptr;
        auto cloned_index = index ? std::unique_ptr<Expr>(static_cast<Expr*>(index->clone())) : nullptr;
        return new AccessExprNode(std::move(cloned_expr), std::move(cloned_index));
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

