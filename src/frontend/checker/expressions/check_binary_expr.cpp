#include "frontend/checker/expressions/check_binary_expr.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

static const char* op_to_dunder(const std::string& op) {
    if (op == "+")  return "__add__";
    if (op == "-")  return "__sub__";
    if (op == "*")  return "__mul__";
    if (op == "/")  return "__div__";
    if (op == "//") return "__floordiv__";
    if (op == "%")  return "__mod__";
    if (op == "**") return "__pow__";
    if (op == "==") return "__eq__";
    if (op == "!=") return "__ne__";
    if (op == "<")  return "__lt__";
    if (op == ">")  return "__gt__";
    if (op == "<=") return "__le__";
    if (op == ">=") return "__ge__";
    return nullptr;
}

std::shared_ptr<nv::Type> check_binary_expr(nv::Checker* ch, Node* node) {
    const auto* bin = static_cast<BinaryExprNode*>(node);
    auto left_type = ch->infer_expr(bin->left.get());

    // Se há erros, parar imediatamente
    if (ch->err) {
        return ch->gettyptr("None");
    }

    auto right_type = ch->infer_expr(bin->right.get());

    // Se há erros, parar imediatamente
    if (ch->err) {
        return ch->gettyptr("None");
    }

    // Resolver tipos antes de unificar
    left_type = ch->unify_ctx.resolve(left_type);
    right_type = ch->unify_ctx.resolve(right_type);

    // Operator overloading para classes definidas pelo usuário
    if (left_type->kind == nv::Kind::CLASS) {
        auto* cls = static_cast<nv::Class*>(left_type.get());
        const char* dunder = op_to_dunder(bin->op);
        if (dunder) {
            auto method_type = cls->get_method(dunder);
            if (method_type) {
                // Anotar o nó para o codegen usar sem re-invocar o checker
                auto* mut_bin = const_cast<BinaryExprNode*>(bin);
                mut_bin->overload_class  = cls->name;
                mut_bin->overload_dunder = dunder;

                auto resolved = ch->unify_ctx.resolve(method_type);
                if (resolved && resolved->kind == nv::Kind::FUNCTION) {
                    return static_cast<nv::Function*>(resolved.get())->returntype;
                }
                return resolved ? resolved : ch->gettyptr("None");
            }
            ch->error(node, "Class '" + cls->name + "' does not implement operator '" +
                      bin->op + "' (method " + dunder + " not found)");
            return ch->gettyptr("None");
        }
    }

    //  Tensor operations 
    bool left_is_tensor  = left_type->kind  == nv::Kind::TENSOR;
    bool right_is_tensor = right_type->kind == nv::Kind::TENSOR;

    if (left_is_tensor || right_is_tensor) {
        auto* lt = left_is_tensor
                   ? static_cast<nv::TensorType*>(left_type.get()) : nullptr;
        auto* rt = right_is_tensor
                   ? static_cast<nv::TensorType*>(right_type.get()) : nullptr;

        auto* mut_bin = const_cast<BinaryExprNode*>(bin);

        // Matmul: Tensor<T,[M,K]> @ Tensor<T,[K,N]> → Tensor<T,[M,N]>
        if (bin->op == "@") {
            if (!lt || !rt) {
                ch->error(node, "Operator '@' requires two tensor operands");
                return ch->gettyptr("None");
            }
            auto result = lt->matmul_result(*rt);
            if (!result) {
                ch->error(node, "Shape mismatch in '@': "
                          + lt->toString() + " @ " + rt->toString());
                return ch->gettyptr("None");
            }
            mut_bin->tensor_op      = "matmul";
            mut_bin->lhs_tensor_dims = lt->dims;
            mut_bin->rhs_tensor_dims = rt->dims;
            ch->types[result->toString()] = result;
            return ch->types[result->toString()];
        }

        // Element-wise arithmetic: +, -, *, /
        if (bin->op == "+" || bin->op == "-" || bin->op == "*" || bin->op == "/") {
            // Scalar broadcast: Tensor op scalar
            if (lt && !rt) {
                mut_bin->tensor_op      = "scalar_mul";
                mut_bin->lhs_tensor_dims = lt->dims;
                auto res = std::make_shared<nv::TensorType>(lt->element, lt->dims);
                ch->types[res->toString()] = res;
                return ch->types[res->toString()];
            }
            if (rt && !lt) {
                mut_bin->tensor_op      = "scalar_mul";
                mut_bin->lhs_tensor_dims = rt->dims;
                auto res = std::make_shared<nv::TensorType>(rt->element, rt->dims);
                ch->types[res->toString()] = res;
                return ch->types[res->toString()];
            }
            // Both tensors: shapes must match (or broadcast)
            if (lt && rt) {
                if (!lt->equals(*rt)) {
                    ch->error(node, "Shape mismatch in '" + bin->op + "': "
                              + lt->toString() + " vs " + rt->toString());
                    return ch->gettyptr("None");
                }
                mut_bin->tensor_op      = (bin->op == "+") ? "add"
                                        : (bin->op == "-") ? "sub"
                                        : (bin->op == "*") ? "mul" : "div";
                mut_bin->lhs_tensor_dims = lt->dims;
                mut_bin->rhs_tensor_dims = rt->dims;
                auto res = std::make_shared<nv::TensorType>(lt->element, lt->dims);
                ch->types[res->toString()] = res;
                return ch->types[res->toString()];
            }
        }

        ch->error(node, "Unsupported operator '" + bin->op + "' for tensor types");
        return ch->gettyptr("None");
    }

    // Verificar casos especiais antes de unificar
    bool left_is_int = left_type->kind == nv::Kind::INT;
    bool left_is_float = left_type->kind == nv::Kind::FLOAT;
    bool left_is_string = left_type->kind == nv::Kind::STRING;
    bool right_is_int = right_type->kind == nv::Kind::INT;
    bool right_is_float = right_type->kind == nv::Kind::FLOAT;
    bool right_is_string = right_type->kind == nv::Kind::STRING;
    
    // Caso especial: multiplicação de string por inteiro (repetição)
    if (bin->op == "*") {
        if ((left_is_string && right_is_int) || (left_is_int && right_is_string)) {
            // string * int ou int * string retorna string
            return ch->gettyptr("str");
        }
    }
    
    // Caso especial: sobrecarga de operadores para "+" (como Python __add__)
    if (bin->op == "+") {
        // String + qualquer tipo -> concatenação (retorna string)
        if (left_is_string) {
            return ch->gettyptr("str");
        }
        // Qualquer tipo + String -> concatenação (retorna string)
        if (right_is_string) {
            return ch->gettyptr("str");
        }
        // Int + Int -> soma (retorna int)
        if (left_is_int && right_is_int) {
            return ch->gettyptr("int");
        }
        // Float + Float -> soma (retorna float)
        if (left_is_float && right_is_float) {
            return ch->gettyptr("float");
        }
        // Int + Float ou Float + Int -> soma com promoção (retorna float)
        if ((left_is_int && right_is_float) || (left_is_float && right_is_int)) {
            return ch->gettyptr("float");
        }
    }
    
    // Se um é int e outro é float, promover int para float
    if (left_is_int && right_is_float) {
        left_type = ch->gettyptr("float");
    } else if (left_is_float && right_is_int) {
        right_type = ch->gettyptr("float");
    }
    
    // Unificar tipos dos operandos
    try {
        ch->unify_ctx.unify(left_type, right_type);
    } catch (std::runtime_error& e) {
        // Usar error() ao invés de throw para evitar duplicação de erros
        ch->error(node, "Binary expression type error: " + std::string(e.what()));
        return ch->gettyptr("None");
    }
    
    // Resolver tipos após unificação
    left_type = ch->unify_ctx.resolve(left_type);
    
    // Determinar tipo de retorno baseado no operador
    if (bin->op == "+" || bin->op == "-" || bin->op == "*" || bin->op == "/" || bin->op == "%") {
        // Operadores aritméticos retornam o tipo dos operandos (promovido se necessário)
        return left_type;
    } else if (bin->op == "**") {
        // O operador de potência (**) sempre retorna float no codegen (usa llvm.pow.f64)
        return ch->gettyptr("float");
    } else if (bin->op == "==" || bin->op == "!=" || bin->op == "<" || 
               bin->op == ">" || bin->op == "<=" || bin->op == ">=") {
        // Operadores de comparação retornam bool
        return ch->gettyptr("bool");
    } else if (bin->op == "&&" || bin->op == "||") {
        // Operadores lógicos retornam bool - ambos operandos devem ser bool
        ch->unify_ctx.unify(left_type, ch->gettyptr("bool"));
        ch->unify_ctx.unify(right_type, ch->gettyptr("bool"));
        return ch->gettyptr("bool");
    }
    
    return left_type;
}
