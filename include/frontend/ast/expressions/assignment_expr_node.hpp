#pragma once
#include "../types.hpp"
#include <memory>

class AssignmentExprNode : public Expr {
public:
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> value;

    AssignmentExprNode(std::unique_ptr<Expr> tgt, std::unique_ptr<Expr> val)
        : Expr(NodeType::AssignmentExpression), target(std::move(tgt)), value(std::move(val)) {}

    ~AssignmentExprNode() override = default;

    Node* clone() const override {
        auto cloned_target = target ? std::unique_ptr<Expr>(static_cast<Expr*>(target->clone())) : nullptr;
        auto cloned_value = value ? std::unique_ptr<Expr>(static_cast<Expr*>(value->clone())) : nullptr;
        return new AssignmentExprNode(std::move(cloned_target), std::move(cloned_value));
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

