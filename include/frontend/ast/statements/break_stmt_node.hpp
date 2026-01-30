#pragma once
#include "../types.hpp"

class BreakStmtNode : public Stmt {
public:
    BreakStmtNode() : Stmt(NodeType::BreakStatement) {}

    ~BreakStmtNode() override = default;
    
    Node* clone() const override {
        auto* node = new BreakStmtNode();
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};
