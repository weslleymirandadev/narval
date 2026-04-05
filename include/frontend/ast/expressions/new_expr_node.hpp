#pragma once
#include "frontend/ast/types.hpp"
#include <vector>
#include <memory>

// Nó para expressão new (instanciação de classe)
class NewExprNode : public Expr {
public:
    std::string class_name;
    std::vector<std::unique_ptr<Expr>> arguments;
    
    NewExprNode(const std::string& class_name)
        : Expr(NodeType::NewExpression), class_name(class_name) {}
    
    Node* clone() const override {
        auto* node = new NewExprNode(class_name);
        for (const auto& arg : arguments) {
            node->arguments.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(arg->clone())));
        }
        return node;
    }
};
