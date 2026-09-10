#pragma once
#include "../types.hpp"
#include <memory>
#include <string>
#include <vector>

// Zig-style compile-time builtin: @typeName(T), @hasField(T, "f"),
// @fieldNames(T), @fields(T), @methods(T), @kind(T), @TypeOf(expr),
// @sizeOf(T), @compileError("msg"). Resolved by the ComptimeEvaluator.
class BuiltinCallNode : public Expr {
public:
    std::string name;
    std::vector<std::unique_ptr<Expr>> args;

    BuiltinCallNode(std::string name, std::vector<std::unique_ptr<Expr>> args)
        : Expr(NodeType::BuiltinCall), name(std::move(name)), args(std::move(args)) {}

    ~BuiltinCallNode() override = default;

    Node* clone() const override {
        std::vector<std::unique_ptr<Expr>> cloned;
        for (const auto& a : args)
            cloned.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(a->clone())));
        auto* node = new BuiltinCallNode(name, std::move(cloned));
        if (position) node->position = std::make_unique<PositionData>(*position);
        return node;
    }
};
