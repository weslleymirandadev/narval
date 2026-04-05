#pragma once
#include "frontend/ast/types.hpp"

// Nó para expressão super
class SuperExprNode : public Expr {
public:
    SuperExprNode() : Expr(NodeType::SuperExpression) {}
    
    Node* clone() const override {
        return new SuperExprNode();
    }
};
