#pragma once
#include "../types.hpp"
#include <memory>

class RangeExprNode : public Expr {
public:
    std::unique_ptr<Expr> start;
    std::unique_ptr<Expr> end;
    bool inclusive;

    RangeExprNode(std::unique_ptr<Expr> start,
                  std::unique_ptr<Expr> end,
                  bool inclusive)
        : Expr(NodeType::RangeExpression),
          start(std::move(start)),
          end(std::move(end)),
          inclusive(inclusive) {}

    ~RangeExprNode() override = default;

    Node* clone() const override {
        auto s = start ? std::unique_ptr<Expr>(static_cast<Expr*>(start->clone())) : nullptr;
        auto e = end ? std::unique_ptr<Expr>(static_cast<Expr*>(end->clone())) : nullptr;
        auto* node = new RangeExprNode(std::move(s), std::move(e), inclusive);
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};
