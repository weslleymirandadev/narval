#pragma once
#include "../types.hpp"

class ContinueStmtNode : public Stmt {
public:
    ContinueStmtNode() : Stmt(NodeType::ContinueStatement) {}

    ~ContinueStmtNode() override = default;
    
    Node* clone() const override {
        auto* node = new ContinueStmtNode();
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};
