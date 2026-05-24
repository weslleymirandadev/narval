#pragma once
#include "frontend/ast/types.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
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
    bool is_override = false;
    std::unique_ptr<Node> method_def;
    
    ClassMethodNode(const std::string& name, const std::string& access_modifier, std::unique_ptr<Node> method_def, bool is_override = false)
        : Expr(NodeType::ClassMethod), name(name), access_modifier(access_modifier), is_override(is_override), method_def(std::move(method_def)) {}
    
    Node* clone() const override {
        auto cloned_method = method_def ? std::unique_ptr<Node>(method_def->clone()) : nullptr;
        return new ClassMethodNode(name, access_modifier, std::move(cloned_method), is_override);
    }
};

// Nó para definição de classe
class ClassStmtNode : public Stmt {
public:
    std::string name;
    std::vector<std::string> type_params; // generic type parameters: class Foo<T, E>
    std::string parent_class;
    std::unique_ptr<PositionData> parent_position;
    bool is_abstract = false;
    std::vector<std::string> implements_interfaces; // implements A, B, C
    std::vector<std::unique_ptr<PositionData>> implements_positions;
    std::vector<std::unique_ptr<ClassFieldNode>> fields;
    std::vector<std::unique_ptr<ClassMethodNode>> methods;
    
    ClassStmtNode(const std::string& name)
        : Stmt(NodeType::ClassStatement), name(name) {}
    
    Node* clone() const override {
        auto* node = new ClassStmtNode(name);
        node->type_params = type_params;
        node->parent_class = parent_class;
        if (parent_position) {
            node->parent_position = std::make_unique<PositionData>(*parent_position);
        }
        node->is_abstract = is_abstract;
        node->implements_interfaces = implements_interfaces;
        for (const auto& implements_position : implements_positions) {
            node->implements_positions.push_back(
                implements_position ? std::make_unique<PositionData>(*implements_position) : nullptr);
        }
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
    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};
