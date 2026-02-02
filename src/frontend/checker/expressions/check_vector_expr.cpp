#include "frontend/checker/expressions/check_vector_expr.hpp"
#include "frontend/ast/expressions/vector_expr_node.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type> check_vector_expr(nv::Checker* ch, Node* node) {
    // VectorExpression sempre cria Vector (tamanho variável, heterogêneo)
    return std::make_shared<nv::Vector>();
}
