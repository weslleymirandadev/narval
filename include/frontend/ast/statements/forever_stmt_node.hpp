#pragma once
#include "../types.hpp"
#include <vector>
#include <memory>

class ForeverStmtNode : public Stmt {
public:
    CodeBlock body;

    ForeverStmtNode(std::vector<std::unique_ptr<Stmt>> body) : Stmt(NodeType::ForeverStatement), body(std::move(body)) {}

    ~ForeverStmtNode() override = default;
    
    Node* clone() const override {
        std::vector<std::unique_ptr<Stmt>> cloned_body;
        cloned_body.reserve(body.size());
        for (const auto& stmt : body) {
            cloned_body.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
        }

        auto* node = new ForeverStmtNode(
            std::move(cloned_body)
        );
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};

