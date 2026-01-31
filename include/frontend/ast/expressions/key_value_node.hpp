#pragma once
#include "../types.hpp"
#include <string>
#include <memory>

class KeyValueNode : public Expr {
public:
    std::unique_ptr<Expr> key;
    std::unique_ptr<Expr> value;

    KeyValueNode(std::unique_ptr<Expr> key, std::unique_ptr<Expr> value)
        : Expr(NodeType::KeyValue), key(std::move(key)), value(std::move(value)) {}

    ~KeyValueNode() override = default;

    Node* clone() const override {
        auto cloned_key = key ? std::unique_ptr<Expr>(static_cast<Expr*>(key->clone())) : nullptr;
        auto cloned_value = value ? std::unique_ptr<Expr>(static_cast<Expr*>(value->clone())) : nullptr;
        auto* node = new KeyValueNode(std::move(cloned_key), std::move(cloned_value));
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};

