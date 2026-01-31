#pragma once
#include "../types.hpp"
#include <vector>
#include <memory>

class MatchStmtNode : public Stmt {
  public:
    std::unique_ptr<Expr> target;
    std::vector<std::unique_ptr<Expr>> cases;
    std::vector<CodeBlock> bodies;

    MatchStmtNode(std::unique_ptr<Expr> tgt, std::vector<std::unique_ptr<Expr>> cases, std::vector<CodeBlock> bodies)
    : Stmt(NodeType::MatchStatement), target(std::move(tgt)), cases(std::move(cases)), bodies(std::move(bodies)) {}

    ~MatchStmtNode() override = default;
    
    Node* clone() const override {
        std::unique_ptr<Expr> mark = std::unique_ptr<Expr>(static_cast<Expr*>(target->clone()));
        std::vector<std::unique_ptr<Expr>> casen;
        for (const auto& e : cases) {
            casen.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(e->clone())));
        }
        std::vector<CodeBlock> bodas;
        for (const auto& e : bodies) {
            CodeBlock body;
            for (const auto& t : e) {
                body.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(t->clone())));
            }
            bodas.push_back(std::move(body));
        }
        auto* node = new MatchStmtNode(std::move(mark), std::move(casen), std::move(bodas));
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};

