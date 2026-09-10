#pragma once
#include "../types.hpp"
#include <memory>
#include <string>

// comptime NAME = <expr>
// Evaluated by the checker; replaced by a constant declaration holding the
// resulting literal, so codegen never sees this node.
class ComptimeDeclNode : public Stmt {
public:
    std::string name;
    std::unique_ptr<Expr> value;

    ComptimeDeclNode(std::string name, std::unique_ptr<Expr> value)
        : Stmt(NodeType::ComptimeDecl),
          name(std::move(name)), value(std::move(value)) {}

    ~ComptimeDeclNode() override = default;

    Node* clone() const override {
        auto cloned = value ? std::unique_ptr<Expr>(static_cast<Expr*>(value->clone())) : nullptr;
        auto* node = new ComptimeDeclNode(name, std::move(cloned));
        if (position) node->position = std::make_unique<PositionData>(*position);
        return node;
    }
};
