#pragma once
#include "../types.hpp"
#include <memory>

class MemberExprNode : public Expr {
public:
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> property;
    // Class declaring the method, filled in by the checker from the receiver's static
    // type. The codegen used to look the owner up by method NAME only, which is
    // ambiguous the moment two classes declare the same one (`__str__`, `clone`, ...):
    // it returned "no owner" and the call failed to compile.
    std::string resolved_owner;

    MemberExprNode(std::unique_ptr<Expr> object, std::unique_ptr<Expr> property)
        : Expr(NodeType::MemberExpression), object(std::move(object)), property(std::move(property)) {}

    ~MemberExprNode() override = default;

    Node* clone() const override {
        auto cloned_object = object ? std::unique_ptr<Expr>(static_cast<Expr*>(object->clone())) : nullptr;
        auto cloned_property = property ? std::unique_ptr<Expr>(static_cast<Expr*>(property->clone())) : nullptr;
        auto* node = new MemberExprNode(std::move(cloned_object), std::move(cloned_property));
        node->resolved_owner = resolved_owner;
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};

