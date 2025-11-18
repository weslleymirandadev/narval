#pragma once
#include "../types.hpp"
#include <vector>
#include <memory>

class MapNode : public Expr {
public:
    std::vector<std::unique_ptr<Expr>> properties;

    MapNode(std::vector<std::unique_ptr<Expr>> properties)
        : Expr(NodeType::Map), properties(std::move(properties)) {}

    ~MapNode() override = default;

    Node* clone() const override {
        std::vector<std::unique_ptr<Expr>> cloned_properties;
        for (const auto& property : properties) {
            cloned_properties.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(property->clone())));
        }
        auto* node = new MapNode(std::move(cloned_properties));
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

