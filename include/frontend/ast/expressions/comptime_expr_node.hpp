#pragma once
#include "../types.hpp"
#include <memory>

// comptime <expr> used inline — evaluated and replaced by its literal.
class ComptimeExprNode : public Expr {
public:
    std::unique_ptr<Expr> inner;

    explicit ComptimeExprNode(std::unique_ptr<Expr> inner)
        : Expr(NodeType::ComptimeExpr), inner(std::move(inner)) {}

    ~ComptimeExprNode() override = default;

    Node* clone() const override {
        auto cloned = inner ? std::unique_ptr<Expr>(static_cast<Expr*>(inner->clone())) : nullptr;
        auto* node = new ComptimeExprNode(std::move(cloned));
        if (position) node->position = std::make_unique<PositionData>(*position);
        return node;
    }
};
