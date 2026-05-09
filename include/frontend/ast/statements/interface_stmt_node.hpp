#pragma once
#include "frontend/ast/types.hpp"
#include <string>
#include <vector>

// Assinatura de método em uma interface
struct InterfaceMethodSig {
    std::string name;
    std::vector<std::pair<std::string, std::string>> params; // (param_name, param_type)
    std::string return_type; // "void" por padrão
};

// Interface — puramente compile-time, sem código gerado
class InterfaceStmtNode : public Stmt {
public:
    std::string name;
    std::vector<std::string> parent_interfaces; // extends A, B
    std::vector<InterfaceMethodSig> methods;

    InterfaceStmtNode(const std::string& iname)
        : Stmt(NodeType::InterfaceStatement), name(iname) {}

    Node* clone() const override {
        auto* node = new InterfaceStmtNode(name);
        node->parent_interfaces = parent_interfaces;
        node->methods = methods;
        if (position) node->position = std::make_unique<PositionData>(*position);
        return node;
    }

    // Interfaces não geram código — compile-time only
    void codegen(nv::IRGenerationContext&) override {}
};
