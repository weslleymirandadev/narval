#pragma once
#include "../types.hpp"
#include <memory>

// inline while COND { ... } — unrolled while: the condition must be
// compile-time known and the body is emitted once per true evaluation.
class ComptimeWhileNode : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    CodeBlock body;

    ComptimeWhileNode(std::unique_ptr<Expr> condition, CodeBlock body)
        : Stmt(NodeType::ComptimeWhile),
          condition(std::move(condition)), body(std::move(body)) {}

    ~ComptimeWhileNode() override = default;

    Node* clone() const override {
        auto c = condition ? std::unique_ptr<Expr>(static_cast<Expr*>(condition->clone())) : nullptr;
        CodeBlock cloned;
        for (const auto& stmt : body)
            cloned.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
        auto* node = new ComptimeWhileNode(std::move(c), std::move(cloned));
        if (position) node->position = std::make_unique<PositionData>(*position);
        return node;
    }
};
