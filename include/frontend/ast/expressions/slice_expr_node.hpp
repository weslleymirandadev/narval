#pragma once
#include "../types.hpp"
#include <memory>

// Representa lista[start:stop:step] — qualquer componente pode ser nullptr (não especificado)
class SliceExprNode : public Expr {
public:
    std::unique_ptr<Expr> collection;
    std::unique_ptr<Expr> start;
    std::unique_ptr<Expr> stop;
    std::unique_ptr<Expr> step;

    SliceExprNode(
        std::unique_ptr<Expr> collection,
        std::unique_ptr<Expr> start,
        std::unique_ptr<Expr> stop,
        std::unique_ptr<Expr> step
    )
        : Expr(NodeType::SliceExpression),
          collection(std::move(collection)),
          start(std::move(start)),
          stop(std::move(stop)),
          step(std::move(step)) {}

    ~SliceExprNode() override = default;

    Node* clone() const override {
        auto c_col   = collection ? std::unique_ptr<Expr>(static_cast<Expr*>(collection->clone())) : nullptr;
        auto c_start = start      ? std::unique_ptr<Expr>(static_cast<Expr*>(start->clone()))      : nullptr;
        auto c_stop  = stop       ? std::unique_ptr<Expr>(static_cast<Expr*>(stop->clone()))       : nullptr;
        auto c_step  = step       ? std::unique_ptr<Expr>(static_cast<Expr*>(step->clone()))       : nullptr;
        auto* node = new SliceExprNode(std::move(c_col), std::move(c_start), std::move(c_stop), std::move(c_step));
        if (position) node->position = std::make_unique<PositionData>(*position);
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};
