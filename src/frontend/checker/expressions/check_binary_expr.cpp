#include "frontend/checker/expressions/check_binary_expr.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type> check_binary_expr(nv::Checker* ch, Node* node) {
    const auto* bin = static_cast<BinaryExprNode*>(node);
    auto left_type = ch->infer_expr(bin->left.get());
    
    // Se há erros, parar imediatamente
    if (ch->err) {
        return ch->gettyptr("void");
    }
    
    auto right_type = ch->infer_expr(bin->right.get());
    
    // Se há erros, parar imediatamente
    if (ch->err) {
        return ch->gettyptr("void");
    }
    
    // Resolver tipos antes de unificar
    left_type = ch->unify_ctx.resolve(left_type);
    right_type = ch->unify_ctx.resolve(right_type);
    
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
<<<<<<< HEAD
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
=======
            return ch->gettyptr("string");
>>>>>>> 7d7b28c04a119a9c000597cd586b6688408f92d1
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
        return ch->gettyptr("void");
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
        // Operadores lógicos retornam bool
        ch->unify_ctx.unify(left_type, ch->gettyptr("bool"));
        return ch->gettyptr("bool");
    }
    
    return left_type;
}
