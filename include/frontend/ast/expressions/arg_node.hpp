#pragma once
#include "../types.hpp"
#include <memory>
#include <string>

class ArgNode : public Expr {
public:
    // Empty name => positional argument
    std::string name;
    std::unique_ptr<Expr> value;

    ArgNode(std::string name, std::unique_ptr<Expr> val)
        : Expr(NodeType::Argument), name(std::move(name)), value(std::move(val)) {}

    ArgNode(const ArgNode& other)
        : Expr(NodeType::Argument), name(other.name) {
        if (other.value) value.reset(static_cast<Expr*>(other.value->clone()));
    }

    ~ArgNode() override = default;

    Node* clone() const override {
        std::unique_ptr<Expr> cloned_val = value ? std::unique_ptr<Expr>(static_cast<Expr*>(value->clone())) : nullptr;
        auto* node = new ArgNode(name, std::move(cloned_val));
        if (position) node->position = std::make_unique<PositionData>(*position);
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override {
        if (value) value->codegen(ctx);
    }
};
