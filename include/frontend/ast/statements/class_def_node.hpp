#pragma once
#include "frontend/ast/types.hpp"
#include "frontend/ast/statements/def_stmt_node.hpp"
#include <vector>
#include <memory>
#include <string>

// Nó para campos de classe
class ClassFieldNode : public Expr {
public:
    std::string name;
    std::string type;
    bool is_mutable;
    
    ClassFieldNode(const std::string& name, const std::string& type, bool is_mutable = false)
        : Expr(NodeType::ClassField), name(name), type(type), is_mutable(is_mutable) {}
    
    Node* clone() const override {
        return new ClassFieldNode(name, type, is_mutable);
    }
};

// Nó para métodos de classe
class ClassMethodNode : public Expr {
public:
    std::string name;
    std::string access_modifier; // "public", "private", "protected"
    std::unique_ptr<Node> method_def;
    
    ClassMethodNode(const std::string& name, const std::string& access_modifier, std::unique_ptr<Node> method_def)
        : Expr(NodeType::ClassMethod), name(name), access_modifier(access_modifier), method_def(std::move(method_def)) {}
    
    Node* clone() const override {
        auto cloned_method = method_def ? std::unique_ptr<Node>(method_def->clone()) : nullptr;
        return new ClassMethodNode(name, access_modifier, std::move(cloned_method));
    }
};

// Nó para definição de classe
class ClassDefNode : public Stmt {
public:
    std::string name;
    std::string parent_class; // Para herança
    std::vector<std::unique_ptr<ClassFieldNode>> fields;
    std::vector<std::unique_ptr<ClassMethodNode>> methods;
    
    ClassDefNode(const std::string& name)
        : Stmt(NodeType::ClassDef), name(name) {}
    
    Node* clone() const override {
        auto* node = new ClassDefNode(name);
        node->parent_class = parent_class;
        for (const auto& field : fields) {
            node->fields.push_back(std::unique_ptr<ClassFieldNode>(static_cast<ClassFieldNode*>(field->clone())));
        }
        for (const auto& method : methods) {
            node->methods.push_back(std::unique_ptr<ClassMethodNode>(static_cast<ClassMethodNode*>(method->clone())));
        }
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};
