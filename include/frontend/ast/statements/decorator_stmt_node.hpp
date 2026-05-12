#pragma once
#include "frontend/ast/types.hpp"
#include <vector>
#include <string>
#include <memory>

namespace nv { class IRGenerationContext; }

struct DecoratorStmtNode : public Stmt {
    std::vector<std::string> decorators;

    DecoratorStmtNode() : Stmt(NodeType::DecoratorStatement) {}

    Node* clone() const override {
        auto* n = new DecoratorStmtNode();
        n->decorators = decorators;
        if (position) n->position = std::make_unique<PositionData>(*position);
        return n;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};
