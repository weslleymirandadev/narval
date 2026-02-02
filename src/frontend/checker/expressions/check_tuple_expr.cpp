#include "frontend/checker/expressions/check_tuple_expr.hpp"
#include "frontend/ast/expressions/tuple_expr_node.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type> check_tuple_expr(nv::Checker* ch, Node* node) {
    const auto* tup = static_cast<TupleExprNode*>(node);
    std::vector<std::shared_ptr<nv::Type>> elem_types;
    for (const auto& elem : tup->elements) {
        elem_types.push_back(ch->infer_expr(elem.get()));
    }
    return std::make_shared<nv::Tuple>(elem_types);
}
