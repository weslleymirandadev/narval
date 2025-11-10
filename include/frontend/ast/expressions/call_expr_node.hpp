#pragma once
#include "../types.hpp"
#include <vector>
#include <memory>

class CallExprNode : public Expr {
public:
    std::unique_ptr<Expr> caller;
    std::vector<std::unique_ptr<Expr>> args;

    CallExprNode(std::unique_ptr<Expr> caller, std::vector<std::unique_ptr<Expr>> args)
        : Expr(NodeType::CallExpression), caller(std::move(caller)), args(std::move(args)) {}

    ~CallExprNode() override = default;

    Node* clone() const override {
        auto cloned_caller = caller ? std::unique_ptr<Expr>(static_cast<Expr*>(caller->clone())) : nullptr;
        std::vector<std::unique_ptr<Expr>> cloned_args;
        for (const auto& expr : args) {
            cloned_args.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(expr->clone())));
        }
        return new CallExprNode(std::move(cloned_caller), std::move(cloned_args));
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

