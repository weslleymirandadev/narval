#pragma once
#include "../types.hpp"
#include <vector>
#include <memory>

class ForStmtNode : public Stmt {
public:
    std::vector<std::unique_ptr<Expr>> bindings;
    std::unique_ptr<Expr> range_start;
    std::unique_ptr<Expr> range_end;
    bool range_inclusive = false;
    std::unique_ptr<Expr> iterable;
    CodeBlock body;
    CodeBlock else_block;

    ForStmtNode(
        std::vector<std::unique_ptr<Expr>> bindings,
        std::unique_ptr<Expr> range_start,
        std::unique_ptr<Expr> range_end,
        bool range_inclusive,
        std::unique_ptr<Expr> iterable,
        std::vector<std::unique_ptr<Stmt>> body,
        std::vector<std::unique_ptr<Stmt>> else_block
    ) : Stmt(NodeType::ForStatement),
        bindings(std::move(bindings)),
        range_start(std::move(range_start)),
        range_end(std::move(range_end)),
        range_inclusive(range_inclusive),
        iterable(std::move(iterable)),
        body(std::move(body)),
        else_block(std::move(else_block)) {}

    ~ForStmtNode() override = default;

    Node* clone() const override {
        std::vector<std::unique_ptr<Expr>> cloned_bindings;
        cloned_bindings.reserve(bindings.size());
        for (const auto& binding : bindings) {
            cloned_bindings.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(binding->clone())));
        }

        auto cloned_range_start = range_start ? std::unique_ptr<Expr>(static_cast<Expr*>(range_start->clone())) : nullptr;
        auto cloned_range_end = range_end ? std::unique_ptr<Expr>(static_cast<Expr*>(range_end->clone())) : nullptr;

        auto cloned_iterable = iterable ? std::unique_ptr<Expr>(static_cast<Expr*>(iterable->clone())) : nullptr;

        std::vector<std::unique_ptr<Stmt>> cloned_body;
        cloned_body.reserve(body.size());
        for (const auto& stmt : body) {
            cloned_body.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
        }

        std::vector<std::unique_ptr<Stmt>> cloned_else;
        cloned_else.reserve(else_block.size());
        for (const auto& stmt : else_block) {
            cloned_else.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
        }

        auto* node = new ForStmtNode(
            std::move(cloned_bindings),
            std::move(cloned_range_start),
            std::move(cloned_range_end),
            range_inclusive,
            std::move(cloned_iterable),
            std::move(cloned_body),
            std::move(cloned_else)
        );
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

