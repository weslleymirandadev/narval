#pragma once
#include "../types.hpp"

class CharLiteralNode : public Expr {
public:
    char value;

    explicit CharLiteralNode(char val)
        : Expr(NodeType::CharLiteral), value(val) {}

    ~CharLiteralNode() override = default;

    Node* clone() const override {
        auto* node = new CharLiteralNode(value);
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};
