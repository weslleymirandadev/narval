#pragma once
#include "../types.hpp"
#include <memory>

class ReturnStmtNode : public Stmt {
public:
    std::unique_ptr<Expr> value;

    ReturnStmtNode(std::unique_ptr<Expr> val)
        : Stmt(NodeType::ReturnStatement), value(std::move(val)) {}

    ~ReturnStmtNode() override = default;
    
    Node* clone() const override {
        auto cloned_value = value ? std::unique_ptr<Expr>(static_cast<Expr*>(value->clone())) : nullptr;
        auto* node = new ReturnStmtNode(std::move(cloned_value));
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

