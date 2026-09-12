#include "frontend/checker/statements/check_for_stmt.hpp"
#include "frontend/ast/statements/for_stmt_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/range_expr_node.hpp"
#include "frontend/checker/unification.hpp"
#include "frontend/ast/expressions/access_expr_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace {

// Does this expression fit the element-wise shape the vectorized lowering handles, and
// are its operands float tensors? Both have to hold: the lowering reads the raw element
// pointer of each operand, and a non-tensor operand would be a pointer into nothing.
bool vector_shape_ok(nv::Checker* ch, Node* e, const std::string& index_name,
                     std::unordered_set<std::string>& tensors) {
    if (!e) return false;
    if (e->kind == NodeType::BinaryExpression) {
        auto* b = static_cast<BinaryExprNode*>(e);
        if (b->op.size() != 1 || std::string("+-*/").find(b->op[0]) == std::string::npos)
            return false;
        return vector_shape_ok(ch, b->left.get(), index_name, tensors) &&
               vector_shape_ok(ch, b->right.get(), index_name, tensors);
    }
    if (e->kind == NodeType::NumericLiteral) return true;
    if (e->kind != NodeType::AccessExpression) return false;

    auto* a = static_cast<AccessExprNode*>(e);
    if (!a->expr || a->expr->kind != NodeType::Identifier) return false;
    if (!a->index || a->index->kind != NodeType::Identifier) return false;
    if (static_cast<IdentifierNode*>(a->index.get())->symbol != index_name) return false;

    const std::string& name = static_cast<IdentifierNode*>(a->expr.get())->symbol;
    if (tensors.count(name)) return true;

    // Probing a type must not report anything: the normal path checks the same
    // expression again and a duplicate diagnostic is worse than a missed fast path.
    const bool had_error = ch->err;
    auto type = ch->infer_expr(a->expr.get());
    if (ch->err && !had_error) { ch->err = false; return false; }
    auto resolved = type ? ch->unify_ctx.resolve(type) : nullptr;
    if (!resolved || resolved->kind != nv::Kind::TENSOR) return false;
    tensors.insert(name);
    return true;
}


// The element-wise shape the vectorized lowering handles: the loop body is one statement,
// `C[i] = <expr over T[i] and number literals>`, with the loop variable as the index of
// every access and every operand a tensor. The lowering reads each operand's raw element
// pointer, so a non-tensor operand would be a pointer into nothing.
bool nv_vector_shape_ok(nv::Checker* ch, ForStmtNode* loop) {
    if (loop->body.size() != 1 || !loop->body[0] ||
        loop->body[0]->kind != NodeType::AssignmentExpression)
        return false;
    auto* asg = static_cast<AssignmentExprNode*>(loop->body[0].get());
    if (!asg->value || !asg->target || asg->target->kind != NodeType::AccessExpression)
        return false;

    std::string var;
    if (!loop->bindings.empty() && loop->bindings[0] &&
        loop->bindings[0]->kind == NodeType::Identifier)
        var = static_cast<IdentifierNode*>(loop->bindings[0].get())->symbol;
    if (var.empty()) return false;

    std::unordered_set<std::string> tensors;
    return vector_shape_ok(ch, asg->target.get(), var, tensors) &&
           vector_shape_ok(ch, asg->value.get(), var, tensors);
}

}  // namespace

std::shared_ptr<nv::Type>& check_for_stmt(nv::Checker* ch, Node* node) {
    auto* for_stmt = static_cast<ForStmtNode*>(node);

    // @[vectorize] on the element-wise shape over float tensors: the answer needs the
    // operand types, which exist here and did not exist when the attribute was seen.
    if (for_stmt->vectorize_attr)
        for_stmt->vectorize_ok = nv_vector_shape_ok(ch, for_stmt);
    
    // Criar novo escopo para o loop
    ch->push_scope();
    
    // Verificar se temos bindings ou iterable
    if (!for_stmt->bindings.empty()) {
        // Modo 1: for binding: range/iterable
        // Verificar range ou iterable
        if (for_stmt->range_start && for_stmt->range_end) {
            // Range mode
            auto start_type = ch->infer_expr(for_stmt->range_start.get());
            auto end_type = ch->infer_expr(for_stmt->range_end.get());
            
            start_type = ch->unify_ctx.resolve(start_type);
            end_type = ch->unify_ctx.resolve(end_type);
            
            // Verificar que start e end são compatíveis
            try {
                ch->unify_ctx.unify(start_type, end_type);
            } catch (std::runtime_error& e) {
                ch->error(for_stmt->range_start.get(), 
                          "Range start and end types must be compatible");
                ch->pop_scope();
                return ch->gettyptr("None");
            }
            
            // Verificar que são tipos iteráveis (int ou string)
            bool start_is_int = start_type->kind == nv::Kind::INT;
            bool start_is_string = start_type->kind == nv::Kind::STRING;
            
            if (!start_is_int && !start_is_string) {
                ch->error(for_stmt->range_start.get(), 
                          "Range bounds must be integers or strings");
                ch->pop_scope();
                return ch->gettyptr("None");
            }
            
            // Adicionar bindings ao escopo com tipo inferido do range
            for (auto& binding : for_stmt->bindings) {
                if (binding->kind == NodeType::Identifier) {
                    auto* id = static_cast<IdentifierNode*>(binding.get());
                    ch->scope->put_key(id->symbol, start_type, false);
                }
            }
        } else if (for_stmt->iterable) {
            // Iterable mode
            auto iterable_type = ch->infer_expr(for_stmt->iterable.get());
            iterable_type = ch->unify_ctx.resolve(iterable_type);
            
            // Verificar que é um tipo iterável (Array, Vector, String, Map, Tuple) ou inteiro (0..N-1)
            bool is_iterable = iterable_type->kind == nv::Kind::ARRAY ||
                              iterable_type->kind == nv::Kind::VECTOR ||
                              iterable_type->kind == nv::Kind::STRING ||
                              iterable_type->kind == nv::Kind::MAP ||
                              iterable_type->kind == nv::Kind::TUPLE ||
                              iterable_type->kind == nv::Kind::INT;
            
            if (!is_iterable) {
                ch->error(for_stmt->iterable.get(), 
                          "For loop iterable must be an array, vector, string, map, tuple, or integer");
                ch->pop_scope();
                return ch->gettyptr("None");
            }
            
            // Inferir tipo dos elementos do iterable
            std::shared_ptr<nv::Type> element_type;
            if (iterable_type->kind == nv::Kind::INT) {
                // for i : N => i percorre 0, 1, ..., N-1
                element_type = ch->gettyptr("int");
            } else if (iterable_type->kind == nv::Kind::ARRAY) {
                auto* arr = static_cast<nv::Array*>(iterable_type.get());
                element_type = arr->element_type;
            } else if (iterable_type->kind == nv::Kind::VECTOR) {
                // Vetores Narval são homogêneos em uso típico; usar int como tipo do elemento
                // para que o loop variable possa ser usado como índice sem erro do checker.
                // O runtime garante a conversão correta via Value.
                element_type = ch->gettyptr("int");
            } else if (iterable_type->kind == nv::Kind::STRING) {
                element_type = ch->gettyptr("str");
            } else if (iterable_type->kind == nv::Kind::MAP) {
                // Para Map, o binding recebe uma tupla (key, value)
                auto* map = static_cast<nv::Map*>(iterable_type.get());
                std::vector<std::shared_ptr<nv::Type>> tuple_types = {
                    map->key_type,
                    map->value_type
                };
                element_type = std::make_shared<nv::Tuple>(tuple_types);
                element_type->init_prototype();
            } else if (iterable_type->kind == nv::Kind::TUPLE) {
                // Para Tuple, usar tipo genérico já que pode ter múltiplos elementos
                int next_id = ch->unify_ctx.get_next_var_id();
                element_type = std::make_shared<nv::TypeVar>(next_id);
            }
            
            // Adicionar bindings ao escopo
            for (auto& binding : for_stmt->bindings) {
                if (binding->kind == NodeType::Identifier) {
                    auto* id = static_cast<IdentifierNode*>(binding.get());
                    ch->scope->put_key(id->symbol, element_type, false);
                }
            }
        } else {
            ch->error(node, "For loop requires either a range or an iterable");
            ch->pop_scope();
            return ch->gettyptr("None");
        }
    } else if (for_stmt->iterable) {
        // Modo 2: for iterable (sem bindings explícitos)
        auto iterable_type = ch->infer_expr(for_stmt->iterable.get());
        iterable_type = ch->unify_ctx.resolve(iterable_type);
        
        // Verificar que é um tipo iterável ou inteiro (0..N-1)
        bool is_iterable = iterable_type->kind == nv::Kind::ARRAY ||
                          iterable_type->kind == nv::Kind::VECTOR ||
                          iterable_type->kind == nv::Kind::STRING ||
                          iterable_type->kind == nv::Kind::MAP ||
                          iterable_type->kind == nv::Kind::TUPLE ||
                          iterable_type->kind == nv::Kind::INT;
        
        if (!is_iterable) {
            ch->error(for_stmt->iterable.get(), 
                      "For loop iterable must be an array, vector, string, map, tuple, or integer");
            ch->pop_scope();
            return ch->gettyptr("None");
        }
    } else {
        ch->error(node, "For loop requires bindings or an iterable");
        ch->pop_scope();
        return ch->gettyptr("None");
    }
    
    // Verificar corpo do loop
    for (auto& stmt : for_stmt->body) {
        ch->check_node(stmt.get());
    }
    
    // Verificar bloco else (se existir)
    for (auto& stmt : for_stmt->else_block) {
        ch->check_node(stmt.get());
    }
    
    ch->pop_scope();
    return ch->gettyptr("None");
}
