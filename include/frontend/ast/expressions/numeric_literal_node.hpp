#pragma once
#include "../types.hpp"
#include <string>

class NumericLiteralNode : public Expr {
public:
    std::string value;

    NumericLiteralNode(std::string val)
        : Expr(NodeType::NumericLiteral), value(std::move(val)) {}

    ~NumericLiteralNode() override = default;

    Node* clone() const override {
        auto* node = new NumericLiteralNode(value);
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};

