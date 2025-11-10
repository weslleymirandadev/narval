#pragma once
#include "../types.hpp"
#include <string>

class StringLiteralNode : public Expr {
public:
    std::string value;

    StringLiteralNode(std::string val)
        : Expr(NodeType::StringLiteral), value(std::move(val)) {}

    ~StringLiteralNode() override = default;

    Node* clone() const override {
        return new StringLiteralNode(value);
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

