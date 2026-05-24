#pragma once
#include "frontend/ast/types.hpp"
#include <vector>
#include <memory>

class OrExprNode : public Expr {
public:
    std::unique_ptr<Expr> expr;

    // Block handler: `or { ... }`
    bool is_block_handler;
    std::vector<std::unique_ptr<Stmt>> block_stmts;

    // Value handler: `or value`
    std::unique_ptr<Expr> value_handler;

    // Block constructor
    OrExprNode(std::unique_ptr<Expr> e, std::vector<std::unique_ptr<Stmt>> stmts)
        : Expr(NodeType::OrExpression), expr(std::move(e)),
          is_block_handler(true), block_stmts(std::move(stmts)) {}

    // Value constructor
    OrExprNode(std::unique_ptr<Expr> e, std::unique_ptr<Expr> handler)
        : Expr(NodeType::OrExpression), expr(std::move(e)),
          is_block_handler(false), value_handler(std::move(handler)) {}

    Node* clone() const override {
        if (is_block_handler) {
            std::vector<std::unique_ptr<Stmt>> cloned;
            for (const auto& s : block_stmts)
                if (s) cloned.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(s->clone())));
            auto* n = new OrExprNode(std::unique_ptr<Expr>(static_cast<Expr*>(expr->clone())), std::move(cloned));
            if (position) n->position = std::make_unique<PositionData>(*position);
            return n;
        } else {
            auto* n = new OrExprNode(
                std::unique_ptr<Expr>(static_cast<Expr*>(expr->clone())),
                value_handler ? std::unique_ptr<Expr>(static_cast<Expr*>(value_handler->clone())) : nullptr);
            if (position) n->position = std::make_unique<PositionData>(*position);
            return n;
        }
    }

    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};
