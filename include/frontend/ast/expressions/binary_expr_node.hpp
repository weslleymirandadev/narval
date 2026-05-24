#pragma once
#include "../types.hpp"
#include <string>
#include <memory>
#include <vector>

class BinaryExprNode : public Expr {
public:
    std::string op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;

    // Preenchido pelo type-checker quando o operador é sobrecarregado por uma classe.
    // Vazio quando é operação primitiva normal.
    std::string overload_class;  // nome da classe (ex: "Vec2")
    std::string overload_dunder; // nome do método (ex: "__add__")

    // Preenchido pelo type-checker quando o operador é tensorial.
    // "matmul", "add", "sub", "scalar_mul" ou "" (não tensor).
    std::string tensor_op;

    // Dims dos operandos tensoriais (usados pelo codegen NIR para selecionar o kernel).
    // Preenchido pelo checker junto com tensor_op.
    std::vector<int64_t> lhs_tensor_dims;
    std::vector<int64_t> rhs_tensor_dims;

    BinaryExprNode(std::string operator_, std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs)
        : Expr(NodeType::BinaryExpression), op(std::move(operator_)), left(std::move(lhs)), right(std::move(rhs)) {}

    ~BinaryExprNode() override = default;

    Node* clone() const override {
        auto cloned_left = left ? std::unique_ptr<Expr>(static_cast<Expr*>(left->clone())) : nullptr;
        auto cloned_right = right ? std::unique_ptr<Expr>(static_cast<Expr*>(right->clone())) : nullptr;
        auto* node = new BinaryExprNode(op, std::move(cloned_left), std::move(cloned_right));
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};

