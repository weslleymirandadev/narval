#pragma once
#include "frontend/ast/types.hpp"

class NoneLiteralNode : public Expr {
public:
    NoneLiteralNode() : Expr(NodeType::NoneLiteral) {}

    Node* clone() const override {
        auto* node = new NoneLiteralNode();
        if (position) node->position = std::make_unique<PositionData>(*position);
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};
