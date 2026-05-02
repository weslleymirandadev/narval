#pragma once
#include "frontend/ast/types.hpp"

// Nó para expressão this
class SelfExprNode : public Expr {
public:
    SelfExprNode() : Expr(NodeType::SelfExpression) {}
    
    Node* clone() const override {
        return new SelfExprNode();
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};
