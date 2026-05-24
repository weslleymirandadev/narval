#pragma once
#include "../types.hpp"
#include <vector>
#include <memory>

class WhileStmtNode : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    CodeBlock body;

    WhileStmtNode(std::unique_ptr<Expr> condition, CodeBlock body)
        : Stmt(NodeType::WhileStatement), condition(std::move(condition)), body(std::move(body)) {}

    ~WhileStmtNode() override = default;

    Node* clone() const override {
        auto cloned_condition = condition ? std::unique_ptr<Expr>(static_cast<Expr*>(condition->clone())) : nullptr;

        CodeBlock cloned_body;
        cloned_body.reserve(body.size());
        for (const auto& stmt : body) {
            cloned_body.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
        }

        auto* node = new WhileStmtNode(
            std::move(cloned_condition),
            std::move(cloned_body)
        );
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};

