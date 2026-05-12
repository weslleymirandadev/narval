#pragma once
#include "frontend/ast/types.hpp"
#include <vector>
#include <string>
#include <memory>

namespace nv { class IRGenerationContext; }

struct AttributeStmtNode : public Stmt {
    std::vector<std::string> attrs;

    AttributeStmtNode() : Stmt(NodeType::AttributeStatement) {}

    Node* clone() const override {
        auto* n = new AttributeStmtNode();
        n->attrs = attrs;
        if (position) n->position = std::make_unique<PositionData>(*position);
        return n;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};
