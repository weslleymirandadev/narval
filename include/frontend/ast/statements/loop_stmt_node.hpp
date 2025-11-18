#pragma once
#include "../types.hpp"
#include <vector>
#include <memory>

class LoopStmtNode : public Stmt {
public:
    CodeBlock body;

    LoopStmtNode(std::vector<std::unique_ptr<Stmt>> body) : Stmt(NodeType::LoopStatement), body(std::move(body)) {}

    ~LoopStmtNode() override = default;
    
    Node* clone() const override {
        std::vector<std::unique_ptr<Stmt>> cloned_body;
        cloned_body.reserve(body.size());
        for (const auto& stmt : body) {
            cloned_body.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
        }

        auto* node = new LoopStmtNode(
            std::move(cloned_body)
        );
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

