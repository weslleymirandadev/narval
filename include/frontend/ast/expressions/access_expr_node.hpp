#pragma once
#include "../types.hpp"
#include <memory>

class AccessExprNode : public Expr {
public:
    std::unique_ptr<Expr> expr;
    std::unique_ptr<Expr> index;
    // Set by the checker for `t[i, j]` on a tensor: several coordinates over one flat
    // buffer. The codegen is untyped, so it needs to be told — it lowers this to a single
    // row-major offset (nv_tensor_flat_index) and lets the ordinary flat access do the rest.
    bool tensor_multi_index = false;

    AccessExprNode(std::unique_ptr<Expr> expr, std::unique_ptr<Expr> index)
        : Expr(NodeType::AccessExpression), expr(std::move(expr)), index(std::move(index)) {}

    ~AccessExprNode() override = default;

    Node* clone() const override {
        auto cloned_expr = expr ? std::unique_ptr<Expr>(static_cast<Expr*>(expr->clone())) : nullptr;
        auto cloned_index = index ? std::unique_ptr<Expr>(static_cast<Expr*>(index->clone())) : nullptr;
        auto* node = new AccessExprNode(std::move(cloned_expr), std::move(cloned_index));
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};

