#pragma once
#include "../types.hpp"
#include <vector>
#include <memory>
#include <utility>

class ListCompNode : public Expr {
public:
    std::unique_ptr<Expr> elt;
    std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> generators;
    std::unique_ptr<Expr> if_cond;
    std::unique_ptr<Expr> else_expr;

    ListCompNode(
        std::unique_ptr<Expr> e,
        std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> gens,
        std::unique_ptr<Expr> cond = nullptr,
        std::unique_ptr<Expr> else_e = nullptr
    ) : Expr(NodeType::ListComprehension),
        elt(std::move(e)),
        generators(std::move(gens)),
        if_cond(std::move(cond)),
        else_expr(std::move(else_e)) {}

    ~ListCompNode() override = default;

    Node* clone() const override {
        std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> cloned_gens;
        for (const auto& [target, source] : generators) {
            cloned_gens.push_back(std::make_pair(
                std::unique_ptr<Expr>(static_cast<Expr*>(target->clone())),
                std::unique_ptr<Expr>(static_cast<Expr*>(source->clone()))
            ));
        }
        return new ListCompNode(
            std::unique_ptr<Expr>(static_cast<Expr*>(elt->clone())),
            std::move(cloned_gens),
            if_cond ? std::unique_ptr<Expr>(static_cast<Expr*>(if_cond->clone())) : nullptr,
            else_expr ? std::unique_ptr<Expr>(static_cast<Expr*>(else_expr->clone())) : nullptr
        );
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

