#pragma once
#include "../types.hpp"
#include <memory>

// comptime if COND { ... } else { ... }
// The branch not taken is dropped from the AST (no IR for it).
class ComptimeIfNode : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    CodeBlock then_body;
    CodeBlock else_body;

    ComptimeIfNode(std::unique_ptr<Expr> condition, CodeBlock then_body, CodeBlock else_body)
        : Stmt(NodeType::ComptimeIf),
          condition(std::move(condition)),
          then_body(std::move(then_body)), else_body(std::move(else_body)) {}

    ~ComptimeIfNode() override = default;

    Node* clone() const override {
        auto cloned_cond = condition ? std::unique_ptr<Expr>(static_cast<Expr*>(condition->clone())) : nullptr;
        CodeBlock t, e;
        for (const auto& stmt : then_body)
            t.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
        for (const auto& stmt : else_body)
            e.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
        auto* node = new ComptimeIfNode(std::move(cloned_cond), std::move(t), std::move(e));
        if (position) node->position = std::make_unique<PositionData>(*position);
        return node;
    }
};
