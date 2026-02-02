#include "frontend/checker/expressions/check_array_expr.hpp"
#include "frontend/ast/expressions/array_expr_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type> check_array_expr(nv::Checker* ch, Node* node) {
    const auto* arr = static_cast<ArrayExprNode*>(node);
    
    // Array vazio ou com tamanho variável → Vector
    if (arr->elements.empty()) {
        return std::make_shared<nv::Vector>();
    }
    
    // Inferir tipo do primeiro elemento
    auto first_type = ch->infer_expr(arr->elements[0].get());
    first_type = ch->unify_ctx.resolve(first_type);
    
    // Tentar unificar todos os elementos com o primeiro
    bool all_same_type = true;
    for (size_t i = 1; i < arr->elements.size(); i++) {
        auto elem_type = ch->infer_expr(arr->elements[i].get());
        elem_type = ch->unify_ctx.resolve(elem_type);
        
        // Verificar se tipos são compatíveis (mesmo tipo ou coerção int->float)
        bool types_compatible = false;
        if (first_type->equals(elem_type)) {
            types_compatible = true;
        } else {
            // Verificar coerção int -> float
            bool first_is_int = first_type->kind == nv::Kind::INT;
            bool first_is_float = first_type->kind == nv::Kind::FLOAT;
            bool elem_is_int = elem_type->kind == nv::Kind::INT;
            bool elem_is_float = elem_type->kind == nv::Kind::FLOAT;
            
            if ((first_is_int && elem_is_float) || (first_is_float && elem_is_int)) {
                types_compatible = true;
                // Promover para float
                if (first_is_int) {
                    first_type = ch->gettyptr("float");
                }
            }
        }
        
        if (!types_compatible) {
            // Tipos incompatíveis → Vector
            all_same_type = false;
            break;
        }
    }
    
    // Se todos têm mesmo tipo (ou coerção válida), criar Array com tamanho fixo
    if (all_same_type) {
        first_type = ch->unify_ctx.resolve(first_type);
        // Verificar se não é variável de tipo não resolvida
        if (first_type->kind != nv::Kind::TYPE_VAR) {
            return std::make_shared<nv::Array>(first_type, arr->elements.size());
        }
    }
    
    // Caso contrário, criar Vector (heterogêneo ou tamanho variável)
    return std::make_shared<nv::Vector>();
}
