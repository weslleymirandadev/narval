#pragma once
#include "frontend/ast/types.hpp"
#include <vector>
#include <string>
#include <memory>


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

    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};
