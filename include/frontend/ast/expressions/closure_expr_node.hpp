#pragma once
#include "frontend/ast/types.hpp"
#include <vector>
#include <memory>

namespace nv { class IRGenerationContext; }

class ClosureExprNode : public Expr {
public:
    std::vector<std::pair<std::string, std::string>> parameters;
    std::string return_type;
    CodeBlock body;
    std::vector<std::string> captures;

    ClosureExprNode() : Expr(NodeType::ClosureExpression) {}

    Node* clone() const override {
        auto* n = new ClosureExprNode();
        n->parameters = parameters;
        n->return_type = return_type;
        for (const auto& s : body)
            n->body.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(s->clone())));
        n->captures = captures;
        if (position) n->position = std::make_unique<PositionData>(*position);
        return n;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};
