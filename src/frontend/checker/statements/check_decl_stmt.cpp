#include "frontend/checker/statements/check_decl_stmt.hpp"
#include "frontend/checker/unification.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/ast/expressions/array_expr_node.hpp"
#include "frontend/ast/expressions/vector_expr_node.hpp"
#include <stdexcept>

namespace {
    // Converte ArrayExpression para VectorExpression
    std::unique_ptr<Expr> convert_array_to_vector(ArrayExprNode* arr_node) {
        std::vector<std::unique_ptr<Expr>> elements;
        for (auto& elem : arr_node->elements) {
            elements.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(elem->clone())));
        }
        auto vec_node = std::make_unique<VectorExprNode>(std::move(elements));
        if (arr_node->position) {
            vec_node->position = std::make_unique<PositionData>(*arr_node->position);
        }
        return vec_node;
    }
    
    // Converte VectorExpression para ArrayExpression
    std::unique_ptr<Expr> convert_vector_to_array(VectorExprNode* vec_node) {
        std::vector<std::unique_ptr<Expr>> elements;
        for (auto& elem : vec_node->elements) {
            elements.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(elem->clone())));
        }
        auto arr_node = std::make_unique<ArrayExprNode>(std::move(elements));
        if (vec_node->position) {
            arr_node->position = std::make_unique<PositionData>(*vec_node->position);
        }
        return arr_node;
    }
}

std::shared_ptr<nv::Type>& check_decl_stmt(nv::Checker* ch, Node* node) {
    auto* decl = static_cast<DeclarationStmtNode*>(node);
    auto* name = static_cast<IdentifierNode*>(decl->target.get());
    
    if (decl->typ == "automatic") {
        // Usar inferência de tipos
        auto inferred_type = ch->infer_expr(decl->value.get());
        
        // Resolver tipo após inferência
        inferred_type = ch->unify_ctx.resolve(inferred_type);
        
        // Generalizar tipo (criar tipo polimórfico se necessário)
        auto free_in_env = ch->get_free_vars_in_env();
        auto generalized = ch->unify_ctx.generalize(inferred_type, free_in_env);
        
        ch->scope->put_key(name->symbol, generalized, decl->constant);
        
        // Retornar referência ao tipo no namespace
        return ch->scope->get_key(name->symbol);
    } else {
        // Tipo explícito - usar verificação tradicional com unificação
        
        // Converter nó baseado no tipo declarado ANTES de inferir
        // Se tipo é vector, converter ArrayExpression para VectorExpression
        // Se tipo é array (int[10]), converter VectorExpression para ArrayExpression
        if (decl->typ == "vector" && decl->value && decl->value->kind == NodeType::ArrayExpression) {
            auto* arr_node = static_cast<ArrayExprNode*>(decl->value.get());
            decl->value = convert_array_to_vector(arr_node);
        } else if (decl->typ.find('[') != std::string::npos && decl->typ.find(']') != std::string::npos) {
            // Tipo é array (int[10], string[5], etc.)
            if (decl->value && decl->value->kind == NodeType::VectorExpression) {
                auto* vec_node = static_cast<VectorExprNode*>(decl->value.get());
                decl->value = convert_vector_to_array(vec_node);
            }
        }
        
        auto& dtype = ch->gettyptr(decl->typ);
        auto vtype = ch->infer_expr(decl->value.get());
        
        // Resolver tipo declarado antes de verificar tamanho (pode haver variáveis de tipo)
        auto resolved_dtype = ch->unify_ctx.resolve(dtype);
        
        // Verificar tamanho de array se o tipo declarado é Array
        if (resolved_dtype->kind == nv::Kind::ARRAY) {
            auto* arr_type = static_cast<nv::Array*>(resolved_dtype.get());
            size_t declared_size = arr_type->size;
            
            // Contar elementos no valor
            size_t actual_elements = 0;
            if (decl->value && decl->value->kind == NodeType::ArrayExpression) {
                auto* arr_node = static_cast<ArrayExprNode*>(decl->value.get());
                actual_elements = arr_node->elements.size();
            } else if (decl->value && decl->value->kind == NodeType::VectorExpression) {
                auto* vec_node = static_cast<VectorExprNode*>(decl->value.get());
                actual_elements = vec_node->elements.size();
            }
            
            if (actual_elements > declared_size) {
                ch->error(decl->value.get(), "Array size mismatch: declared size is " + 
                                       std::to_string(declared_size) + 
                                       ", but " + std::to_string(actual_elements) + 
                                       " elements were provided.");
                return ch->gettyptr("void");
            }
        }
        
        // Unificar tipo inferido com tipo declarado
        try {
            ch->unify_ctx.unify(vtype, dtype);
        } catch (std::runtime_error& e) {
            ch->error(decl->value.get(), std::string("Expected type '") + dtype->toString() + 
                                   "', but got '" + vtype->toString() + "'. " + e.what());
            return ch->gettyptr("void");
        }
        
        // Resolver tipo após unificação
        dtype = ch->unify_ctx.resolve(dtype);
        ch->scope->put_key(name->symbol, dtype, decl->constant);
        return ch->scope->get_key(name->symbol);
    }
}
