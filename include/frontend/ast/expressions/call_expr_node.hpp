#pragma once
#include "../types.hpp"
#include <vector>
#include <memory>
#include "arg_node.hpp"

class CallExprNode : public Expr {
public:
    std::unique_ptr<Expr> caller;
    std::vector<std::unique_ptr<ArgNode>> args;

    CallExprNode(std::unique_ptr<Expr> caller, std::vector<std::unique_ptr<ArgNode>> args)
        : Expr(NodeType::CallExpression), caller(std::move(caller)), args(std::move(args)) {}

    ~CallExprNode() override = default;

    Node* clone() const override {
        auto cloned_caller = caller ? std::unique_ptr<Expr>(static_cast<Expr*>(caller->clone())) : nullptr;
        std::vector<std::unique_ptr<ArgNode>> cloned_args;
        for (const auto& expr : args) {
            cloned_args.push_back(std::unique_ptr<ArgNode>(static_cast<ArgNode*>(expr->clone())));
        }
        auto* node = new CallExprNode(std::move(cloned_caller), std::move(cloned_args));
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};

