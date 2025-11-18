#pragma once
#include "../types.hpp"
#include <memory>

class MemberExprNode : public Expr {
public:
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> property;

    MemberExprNode(std::unique_ptr<Expr> object, std::unique_ptr<Expr> property)
        : Expr(NodeType::MemberExpression), object(std::move(object)), property(std::move(property)) {}

    ~MemberExprNode() override = default;

    Node* clone() const override {
        auto cloned_object = object ? std::unique_ptr<Expr>(static_cast<Expr*>(object->clone())) : nullptr;
        auto cloned_property = property ? std::unique_ptr<Expr>(static_cast<Expr*>(property->clone())) : nullptr;
        auto* node = new MemberExprNode(std::move(cloned_object), std::move(cloned_property));
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

