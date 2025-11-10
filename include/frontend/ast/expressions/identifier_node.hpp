#pragma once
#include "../types.hpp"
#include <string>

class IdentifierNode : public Expr {
public:
    std::string symbol;

    IdentifierNode(std::string sym)
        : Expr(NodeType::Identifier), symbol(std::move(sym)) {}

    ~IdentifierNode() override = default;

    Node* clone() const override {
        return new IdentifierNode(symbol);
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

