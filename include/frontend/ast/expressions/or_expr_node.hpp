#pragma once
#include "frontend/ast/types.hpp"
#include <vector>
#include <memory>

// Represents: expr or { stmts... }   or   expr or value_expr
class OrExprNode : public Expr {
public:
    std::unique_ptr<Node> expr;

    // Block handler: `or { ... }`
    bool is_block_handler;
    std::vector<std::unique_ptr<Node>> block_stmts;

    // Value handler: `or value`
    std::unique_ptr<Node> value_handler;

    // Block constructor
    OrExprNode(std::unique_ptr<Node> e, std::vector<std::unique_ptr<Node>> stmts)
        : Expr(NodeType::OrExpression), expr(std::move(e)),
          is_block_handler(true), block_stmts(std::move(stmts)) {}

    // Value constructor
    OrExprNode(std::unique_ptr<Node> e, std::unique_ptr<Node> handler)
        : Expr(NodeType::OrExpression), expr(std::move(e)),
          is_block_handler(false), value_handler(std::move(handler)) {}

    Node* clone() const override {
        if (is_block_handler) {
            std::vector<std::unique_ptr<Node>> cloned;
            for (const auto& s : block_stmts)
                if (s) cloned.push_back(std::unique_ptr<Node>(s->clone()));
            auto* n = new OrExprNode(std::unique_ptr<Node>(expr->clone()), std::move(cloned));
            if (position) n->position = std::make_unique<PositionData>(*position);
            return n;
        } else {
            auto* n = new OrExprNode(
                std::unique_ptr<Node>(expr->clone()),
                value_handler ? std::unique_ptr<Node>(value_handler->clone()) : nullptr);
            if (position) n->position = std::make_unique<PositionData>(*position);
            return n;
        }
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};
