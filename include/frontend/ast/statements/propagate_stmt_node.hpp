#pragma once
#include "frontend/ast/types.hpp"

class PropagateStmtNode : public Stmt {
public:
    PropagateStmtNode() : Stmt(NodeType::PropagateStatement) {}

    Node* clone() const override {
        auto* n = new PropagateStmtNode();
        if (position) n->position = std::make_unique<PositionData>(*position);
        return n;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};
