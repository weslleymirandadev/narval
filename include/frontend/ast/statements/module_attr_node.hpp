#pragma once
#include "frontend/ast/types.hpp"
#include <vector>
#include <string>
#include <memory>

namespace nv { class IRGenerationContext; }

class ModuleAttrNode : public Stmt {
public:
    std::vector<std::string> attrs;

    ModuleAttrNode() : Stmt(NodeType::ModuleAttrStatement) {}

    Node* clone() const override {
        auto* n = new ModuleAttrNode();
        n->attrs = attrs;
        if (position) n->position = std::make_unique<PositionData>(*position);
        return n;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};
