#pragma once
#include "../types.hpp"
#include <string>

class IdentifierNode : public Expr {
public:
    std::string symbol;

    IdentifierNode(std::string sym)
        : Expr(NodeType::Identifier), symbol(std::move(sym)) {}

    ~IdentifierNode() override;

    Node* clone() const override {
        auto* node = new IdentifierNode(symbol);
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};

