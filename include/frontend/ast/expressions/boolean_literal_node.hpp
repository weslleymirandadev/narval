#pragma once
#include "frontend/ast/types.hpp"

class BooleanLiteralNode: public Expr {
    public:
        bool value;

        BooleanLiteralNode(bool val) :Expr(NodeType::BooleanLiteral), value(val) {}

        ~BooleanLiteralNode() override = default;

        Node* clone() const override {
            return new BooleanLiteralNode(value);
        }

        void codegen(rph::IRGenerationContext& ctx) override;
};