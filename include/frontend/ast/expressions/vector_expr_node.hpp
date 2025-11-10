#pragma once
#include "../types.hpp"
#include <vector>
#include <memory>

class VectorExprNode : public Expr {
public:
    std::vector<std::unique_ptr<Expr>> elements;

    VectorExprNode(std::vector<std::unique_ptr<Expr>> elements)
        : Expr(NodeType::VectorExpression), elements(std::move(elements)) {}

    ~VectorExprNode() override = default;

    Node* clone() const override {
        std::vector<std::unique_ptr<Expr>> cloned_elements;
        for (const auto& element : elements) {
            cloned_elements.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(element->clone())));
        }
        return new VectorExprNode(std::move(cloned_elements));
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

