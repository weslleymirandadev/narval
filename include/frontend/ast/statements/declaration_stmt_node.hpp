#pragma once
#include "../types.hpp"
#include <memory>
#include <string>

class DeclarationStmtNode : public Stmt {
public:
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> value;
    std::string typ;
    bool constant;
    DeclarationStmtNode(std::unique_ptr<Expr> tgt, std::unique_ptr<Expr> val, std::string tyyp, bool locked)
        : Stmt(NodeType::DeclarationStatement), target(std::move(tgt)), value(std::move(val)), typ(tyyp), constant(locked) {};
    
    DeclarationStmtNode(std::unique_ptr<Expr> tgt, std::unique_ptr<Expr> val, std::string tyyp)
        : Stmt(NodeType::DeclarationStatement), target(std::move(tgt)), value(std::move(val)), typ(tyyp), constant(false) {};
    
    ~DeclarationStmtNode() override = default;

    Node* clone() const override {
        auto cloned_target = target ? std::unique_ptr<Expr>(static_cast<Expr*>(target->clone())) : nullptr;
        auto cloned_value = value ? std::unique_ptr<Expr>(static_cast<Expr*>(value->clone())) : nullptr;
        auto* node = new DeclarationStmtNode(std::move(cloned_target), std::move(cloned_value), this->typ, this->constant);
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};

