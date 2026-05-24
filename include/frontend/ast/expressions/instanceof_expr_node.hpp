#pragma once
#include "frontend/ast/types.hpp"
#include <vector>
#include <memory>

// Nó para expressão instanceof
class InstanceofExprNode : public Expr {
public:
    std::unique_ptr<Expr> object;
    std::string class_name;
    
    InstanceofExprNode(std::unique_ptr<Expr> obj, const std::string& class_name)
        : Expr(NodeType::InstanceofExpression), object(std::move(obj)), class_name(class_name) {}
    
    Node* clone() const override {
        auto* node = new InstanceofExprNode(
            std::unique_ptr<Expr>(static_cast<Expr*>(object->clone())),
            class_name
        );
        return node;
    }

    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};
