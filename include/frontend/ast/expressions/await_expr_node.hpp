#pragma once
#include "frontend/ast/types.hpp"
#include <memory>

namespace nv { class IRGenerationContext; }

// `await expr` — suspende a fiber atual até que o Future<T> esteja resolvido,
// retornando o valor interno T.
struct AwaitExprNode : public Expr {
    std::unique_ptr<Node> operand;

    explicit AwaitExprNode(std::unique_ptr<Node> op)
        : Expr(NodeType::AwaitExpression), operand(std::move(op)) {}

    ~AwaitExprNode() override = default;

    Node* clone() const override {
        auto* n = new AwaitExprNode(std::unique_ptr<Node>(operand->clone()));
        if (position) n->position = std::make_unique<PositionData>(*position);
        return n;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};
