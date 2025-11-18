#pragma once
#include "../types.hpp"
#include <vector>
#include <memory>

class ArrayExprNode : public Expr {
public:
    std::vector<std::unique_ptr<Expr>> elements;

    ArrayExprNode(std::vector<std::unique_ptr<Expr>> elements)
        : Expr(NodeType::ArrayExpression), elements(std::move(elements)) {}

    ~ArrayExprNode() override = default;

    Node* clone() const override {
        std::vector<std::unique_ptr<Expr>> cloned_elements;
        for (const auto& element : elements) {
            cloned_elements.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(element->clone())));
        }
        auto* node = new ArrayExprNode(std::move(cloned_elements));
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

