#pragma once
#include "../types.hpp"
#include <vector>
#include <memory>

class IfStatementNode : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    CodeBlock consequent;
    CodeBlock alternate;

    IfStatementNode(std::unique_ptr<Expr> cond, std::vector<std::unique_ptr<Stmt>> consequent_stmts, std::vector<std::unique_ptr<Stmt>> alternate_stmts)
        : Stmt(NodeType::IfStatement), condition(std::move(cond)), consequent(std::move(consequent_stmts)), alternate(std::move(alternate_stmts)) {}

    ~IfStatementNode() override = default;

    Node* clone() const override {
        auto cloned_condition = condition ? std::unique_ptr<Expr>(static_cast<Expr*>(condition->clone())) : nullptr;
        std::vector<std::unique_ptr<Stmt>> cloned_consequent;
        for (const auto& stmt : consequent) {
            cloned_consequent.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
        }
        std::vector<std::unique_ptr<Stmt>> cloned_alternate;
        for (const auto& stmt : alternate) {
            cloned_alternate.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
        }
        return new IfStatementNode(std::move(cloned_condition), std::move(cloned_consequent), std::move(cloned_alternate));
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

