#pragma once
#include "frontend/ast/types.hpp"
#include <vector>
#include <memory>

namespace nv { class IRGenerationContext; }

struct CatchClause {
    std::string exception_type;
    std::string exception_var;
    std::vector<std::unique_ptr<Node>> body;

    CatchClause(const std::string& type, const std::string& var,
                std::vector<std::unique_ptr<Node>> b)
        : exception_type(type), exception_var(var), body(std::move(b)) {}
};

class TryStatementNode : public Stmt {
public:
    std::vector<std::unique_ptr<Node>> try_body;
    std::vector<std::unique_ptr<CatchClause>> catches;
    std::vector<std::unique_ptr<Node>> finally_body;

    TryStatementNode(
        std::vector<std::unique_ptr<Node>> try_body,
        std::vector<std::unique_ptr<CatchClause>> catches,
        std::vector<std::unique_ptr<Node>> finally_body = {}
    ) : Stmt(NodeType::TryStatement),
        try_body(std::move(try_body)),
        catches(std::move(catches)),
        finally_body(std::move(finally_body)) {}

    ~TryStatementNode() override = default;

    Node* clone() const override {
        std::vector<std::unique_ptr<Node>> cloned_try;
        for (const auto& s : try_body)
            if (s) cloned_try.push_back(std::unique_ptr<Node>(s->clone()));

        std::vector<std::unique_ptr<CatchClause>> cloned_catches;
        for (const auto& c : catches) {
            std::vector<std::unique_ptr<Node>> cloned_body;
            for (const auto& s : c->body)
                if (s) cloned_body.push_back(std::unique_ptr<Node>(s->clone()));
            cloned_catches.push_back(std::make_unique<CatchClause>(
                c->exception_type, c->exception_var, std::move(cloned_body)));
        }

        std::vector<std::unique_ptr<Node>> cloned_finally;
        for (const auto& s : finally_body)
            if (s) cloned_finally.push_back(std::unique_ptr<Node>(s->clone()));

        auto* node = new TryStatementNode(
            std::move(cloned_try), std::move(cloned_catches), std::move(cloned_finally));
        if (position)
            node->position = std::make_unique<PositionData>(*position);
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};
