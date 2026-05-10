#pragma once
#include "frontend/ast/ast.hpp"
#include <vector>
#include <memory>

namespace nv { class IRGenerationContext; }

// `defer error { handler }` — intercepta qualquer erro que ocorra nos statements
// restantes do bloco envolvente (remaining_body), executando o handler se houver erro.
// `err` é automaticamente vinculado ao erro capturado dentro do handler.
struct DeferErrorStmtNode : public Stmt {
    std::vector<std::unique_ptr<Node>> handler_body;   // bloco { ... } do defer error
    std::vector<std::unique_ptr<Stmt>> remaining_body; // statements restantes do bloco pai

    DeferErrorStmtNode(
        std::vector<std::unique_ptr<Node>> handler,
        std::vector<std::unique_ptr<Stmt>> remaining = {}
    ) : Stmt(NodeType::DeferErrorStatement),
        handler_body(std::move(handler)),
        remaining_body(std::move(remaining)) {}

    ~DeferErrorStmtNode() override = default;

    Node* clone() const override {
        std::vector<std::unique_ptr<Node>> ch;
        for (const auto& s : handler_body)
            if (s) ch.push_back(std::unique_ptr<Node>(s->clone()));
        std::vector<std::unique_ptr<Stmt>> cr;
        for (const auto& s : remaining_body)
            if (s) cr.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(s->clone())));
        auto* n = new DeferErrorStmtNode(std::move(ch), std::move(cr));
        if (position) n->position = std::make_unique<PositionData>(*position);
        return n;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};
