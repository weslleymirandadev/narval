#pragma once
#include "frontend/ast/types.hpp"

// Nó para expressão this
class ThisExprNode : public Expr {
public:
    ThisExprNode() : Expr(NodeType::ThisExpression) {}
    
    Node* clone() const override {
        return new ThisExprNode();
    }
};
