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
        return new NumericLiteralNode(value);
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

