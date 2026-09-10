#pragma once
#include "../types.hpp"
#include <memory>

// comptime { ... } — every statement inside runs at compile time.
class ComptimeBlockNode : public Stmt {
public:
    CodeBlock body;

    explicit ComptimeBlockNode(CodeBlock body)
        : Stmt(NodeType::ComptimeBlock), body(std::move(body)) {}

    ~ComptimeBlockNode() override = default;

    Node* clone() const override {
        CodeBlock cloned;
        for (const auto& stmt : body)
            cloned.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
        auto* node = new ComptimeBlockNode(std::move(cloned));
        if (position) node->position = std::make_unique<PositionData>(*position);
        return node;
    }
};
