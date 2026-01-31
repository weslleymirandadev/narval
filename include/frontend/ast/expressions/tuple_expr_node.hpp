#pragma once
#include "../types.hpp"
#include <vector>
#include <memory>

class TupleExprNode : public Expr {
public:
    std::vector<std::unique_ptr<Expr>> elements;

    TupleExprNode(std::vector<std::unique_ptr<Expr>> elements)
        : Expr(NodeType::TupleExpression), elements(std::move(elements)) {}

    ~TupleExprNode() override = default;

    Node* clone() const override {
        std::vector<std::unique_ptr<Expr>> cloned_elements;
        for (const auto& element : elements) {
            cloned_elements.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(element->clone())));
        }
        auto* node = new TupleExprNode(std::move(cloned_elements));
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }
    
    void codegen(nv::IRGenerationContext& ctx) override;
};

