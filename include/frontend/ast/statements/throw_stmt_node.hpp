#pragma once
#include "frontend/ast/ast.hpp"
#include <memory>

namespace nv { class IRGenerationContext; }

struct ThrowStatementNode : public Stmt {
    std::unique_ptr<Node> exception;
    
    ThrowStatementNode(std::unique_ptr<Node> exception) 
        : Stmt(NodeType::ThrowStatement), exception(std::move(exception)) {}
    
    ~ThrowStatementNode() override = default;
    
    Node* clone() const override {
        auto cloned_exception = exception ? std::unique_ptr<Node>(exception->clone()) : nullptr;
        
        auto* node = new ThrowStatementNode(std::move(cloned_exception));
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }
    
    void codegen(nv::IRGenerationContext& ctx) override;
};
