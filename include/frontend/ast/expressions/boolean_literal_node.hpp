#pragma once
#include "frontend/ast/types.hpp"

class BooleanLiteralNode: public Expr {
    public:
        bool value;

        BooleanLiteralNode(bool val) :Expr(NodeType::BooleanLiteral), value(val) {}

        ~BooleanLiteralNode() override = default;

        Node* clone() const override {
            auto* node = new BooleanLiteralNode(value);
            if (position) {
                node->position = std::make_unique<PositionData>(*position);
            }
            return node;
        }

        void codegen(rph::IRGenerationContext& ctx) override;
};