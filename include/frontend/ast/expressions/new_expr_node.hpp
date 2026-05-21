#pragma once
#include "frontend/ast/types.hpp"
#include <vector>
#include <memory>
#include <string>

// Nó para expressão new (instanciação de classe)
class NewExprNode : public Expr {
public:
    std::string class_name;      // pode ser "Box<int>" para genéricos
    std::string base_class_name; // sempre "Box" (sem type args) — usado no codegen
    std::vector<std::unique_ptr<Expr>> arguments;

    NewExprNode(const std::string& class_name, const std::string& base_name)
        : Expr(NodeType::NewExpression), class_name(class_name), base_class_name(base_name) {}

    Node* clone() const override {
        auto* node = new NewExprNode(class_name, base_class_name);
        for (const auto& arg : arguments) {
            node->arguments.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(arg->clone())));
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};
