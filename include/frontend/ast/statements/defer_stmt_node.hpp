#pragma once
#include "frontend/ast/ast.hpp"
#include <vector>
#include <memory>


// `defer { body }` — executa body ao sair do escopo (normal ou por exceção, com re-throw).
// remaining_body é preenchido pelo parse_body() do bloco pai.
struct DeferStmtNode : public Stmt {
    std::vector<std::unique_ptr<Node>> defer_body;
    std::vector<std::unique_ptr<Stmt>> remaining_body;

    DeferStmtNode(
        std::vector<std::unique_ptr<Node>> body,
        std::vector<std::unique_ptr<Stmt>> remaining = {}
    ) : Stmt(NodeType::DeferStatement),
        defer_body(std::move(body)),
        remaining_body(std::move(remaining)) {}

    ~DeferStmtNode() override = default;

    Node* clone() const override {
        std::vector<std::unique_ptr<Node>> cb;
        for (const auto& s : defer_body)
            if (s) cb.push_back(std::unique_ptr<Node>(s->clone()));
        std::vector<std::unique_ptr<Stmt>> cr;
        for (const auto& s : remaining_body)
            if (s) cr.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(s->clone())));
        auto* n = new DeferStmtNode(std::move(cb), std::move(cr));
        if (position) n->position = std::make_unique<PositionData>(*position);
        return n;
    }

    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};
