#pragma once
#include "frontend/ast/types.hpp"
#include <string>
#include <vector>

struct EnumVariant {
    std::string name;
    bool has_explicit_value;
    int explicit_value;

    EnumVariant(const std::string& n, bool has_val = false, int val = 0)
        : name(n), has_explicit_value(has_val), explicit_value(val) {}
};

class EnumStmtNode : public Stmt {
public:
    std::string name;
    std::vector<EnumVariant> variants;

    EnumStmtNode(const std::string& enum_name)
        : Stmt(NodeType::EnumStatement), name(enum_name) {}

    Node* clone() const override {
        auto* node = new EnumStmtNode(name);
        node->variants = variants;
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};
