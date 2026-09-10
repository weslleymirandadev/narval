#pragma once
#include "../types.hpp"
#include <memory>
#include <string>

// comptime for VAR in <range> { ... }
// Unrolled by the checker: the body is cloned once per iteration.
class ComptimeForNode : public Stmt {
public:
    std::string var;
    std::unique_ptr<Expr> range;
    CodeBlock body;

    ComptimeForNode(std::string var, std::unique_ptr<Expr> range, CodeBlock body)
        : Stmt(NodeType::ComptimeFor),
          var(std::move(var)), range(std::move(range)), body(std::move(body)) {}

    ~ComptimeForNode() override = default;

    Node* clone() const override {
        auto cloned_range = range ? std::unique_ptr<Expr>(static_cast<Expr*>(range->clone())) : nullptr;
        CodeBlock cloned_body;
        for (const auto& stmt : body)
            cloned_body.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
        auto* node = new ComptimeForNode(var, std::move(cloned_range), std::move(cloned_body));
        if (position) node->position = std::make_unique<PositionData>(*position);
        return node;
    }
};
