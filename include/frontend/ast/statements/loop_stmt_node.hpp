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

        return new LoopStmtNode(
            std::move(cloned_body)
        );
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

