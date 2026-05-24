#pragma once
#include "frontend/ast/types.hpp"
#include <vector>
#include <memory>

namespace nv {
struct Type;
}

class ClosureExprNode : public Expr {
public:
    std::vector<std::pair<std::string, std::string>> parameters;
    std::string return_type;
    CodeBlock body;
    std::vector<std::string> captures;
    std::vector<std::string> type_params; // generic params: <T, E>|x: T|: E { }
    bool type_checked = false;
    std::shared_ptr<nv::Type> cached_type = nullptr;

    ClosureExprNode() : Expr(NodeType::ClosureExpression) {}

    Node* clone() const override {
        auto* n = new ClosureExprNode();
        n->parameters = parameters;
        n->return_type = return_type;
        for (const auto& s : body)
            n->body.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(s->clone())));
        n->captures = captures;
        n->type_params = type_params;
        n->type_checked = type_checked;
        n->cached_type = cached_type;
        if (position) n->position = std::make_unique<PositionData>(*position);
        return n;
    }

    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};
