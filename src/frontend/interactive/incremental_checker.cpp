#include "frontend/interactive/incremental_checker.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "frontend/ast/expressions/call_expr_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/access_expr_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"
#include "frontend/ast/expressions/conditional_expr_node.hpp"
#include "frontend/ast/expressions/logical_not_expr_node.hpp"
#include "frontend/ast/expressions/unary_minus_expr_node.hpp"
#include "frontend/ast/expressions/increment_expr_node.hpp"
#include "frontend/ast/expressions/decrement_expr_node.hpp"
#include "frontend/ast/expressions/post_increment_expr_node.hpp"
#include "frontend/ast/expressions/post_decrement_expr_node.hpp"
#include "frontend/ast/expressions/array_expr_node.hpp"
#include "frontend/ast/expressions/tuple_expr_node.hpp"
#include "frontend/ast/expressions/vector_expr_node.hpp"
#include "frontend/ast/expressions/list_comp_node.hpp"
#include "frontend/ast/expressions/range_expr_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/statements/def_stmt_node.hpp"
#include "frontend/ast/statements/if_statement_node.hpp"
#include "frontend/ast/statements/for_stmt_node.hpp"
#include "frontend/ast/statements/while_stmt_node.hpp"
#include "frontend/ast/statements/loop_stmt_node.hpp"
#include "frontend/ast/statements/match_stmt_node.hpp"
#include "frontend/ast/statements/return_stmt_node.hpp"
#include "frontend/ast/program.hpp"
#include <iostream>
#include <stdexcept>

namespace nv {
namespace interactive {

IncrementalChecker::IncrementalChecker(SessionManager& session_manager)
    : session_manager(session_manager) {
    // Usar o namespace global da sessão desde a inicialização
    // Substituir o namespace global criado pelo construtor do Checker
    if (!checker.namespaces.empty()) {
        checker.namespaces[0] = session_manager.get_global_namespace();
    } else {
        checker.namespaces.push_back(session_manager.get_global_namespace());
    }
    checker.scope = session_manager.get_global_namespace();
    
    // Sincronizar tipos básicos do checker com a sessão
    // Os tipos básicos já estão no namespace da sessão através do Checker padrão
}

bool IncrementalChecker::check_unit(ExecutionUnit& unit) {
    // Resetar estado de erros
    checker.err = false;
    
    // Sincronizar namespace do checker com o da sessão
    // O checker precisa usar o namespace global da sessão
    // Substituir o namespace global do checker pelo da sessão
    if (!checker.namespaces.empty()) {
        checker.namespaces[0] = session_manager.get_global_namespace();
    } else {
        checker.namespaces.push_back(session_manager.get_global_namespace());
    }
    checker.scope = session_manager.get_global_namespace();
    
    // Coletar símbolos definidos e usados
    collect_defined_symbols(unit.get_ast(), unit);
    collect_used_symbols(unit.get_ast(), unit);
    
    // Validar referências a símbolos
    if (!validate_symbol_references(unit)) {
        return false;
    }
    
    // Realizar análise semântica completa
    try {
        checker.check_node(unit.get_ast());
    } catch (...) {
        // Erros já foram reportados via checker.error()
        return false;
    }
    
    // Se houve erros, retornar false
    if (checker.err) {
        return false;
    }
    
    // Registrar símbolos definidos no SessionManager
    for (const auto& symbol_name : unit.get_defined_symbols()) {
        // Obter tipo do símbolo do namespace
        try {
            auto& type = checker.scope->get_key(symbol_name);
            // Verificar se o símbolo já existe (redefinição)
            if (session_manager.has_symbol(symbol_name)) {
                // Atualizar símbolo existente (redefinição)
                session_manager.update_symbol(symbol_name, type, unit.get_id(), unit.get_source_name());
            } else {
                // Definir novo símbolo
                session_manager.define_symbol(symbol_name, type, unit.get_id(), unit.get_source_name());
            }
        } catch (const std::exception& e) {
            // Símbolo não encontrado no namespace (erro interno)
            std::cerr << "Warning: Symbol '" << symbol_name 
                      << "' not found in namespace after checking: " << e.what() << "\n";
        } catch (...) {
            // Símbolo não encontrado no namespace (erro interno)
            std::cerr << "Warning: Symbol '" << symbol_name 
                      << "' not found in namespace after checking\n";
        }
    }
    
    // Atualizar dependências
    update_dependencies(unit);
    
    return true;
}

bool IncrementalChecker::recheck_unit(ExecutionUnit& unit) {
    // Similar a check_unit, mas considera que símbolos podem ter sido invalidados
    return check_unit(unit);
}

void IncrementalChecker::collect_defined_symbols(Node* node, ExecutionUnit& unit) {
    if (!node) return;
    
    switch (node->kind) {
        case NodeType::DeclarationStatement: {
            const auto* decl = static_cast<DeclarationStmtNode*>(node);
            // O target é uma expressão (geralmente um Identifier)
            if (decl->target && decl->target->kind == NodeType::Identifier) {
                const auto* id = static_cast<IdentifierNode*>(decl->target.get());
                unit.add_defined_symbol(id->symbol);
            }
            break;
        }
        case NodeType::DefStatement: {
            const auto* def = static_cast<DefStmtNode*>(node);
            unit.add_defined_symbol(def->name);
            break;
        }
        case NodeType::AssignmentExpression: {
            const auto* assign = static_cast<AssignmentExprNode*>(node);
            if (assign->target->kind == NodeType::Identifier) {
                const auto* id = static_cast<IdentifierNode*>(assign->target.get());
                // Assignment pode ser definição ou atualização
                // Verificaremos depois se o símbolo já existe
                unit.add_defined_symbol(id->symbol);
            }
            break;
        }
        default:
            break;
    }
    
    // Processar filhos recursivamente
    process_node_for_symbols(node, unit);
}

void IncrementalChecker::collect_used_symbols(Node* node, ExecutionUnit& unit) {
    if (!node) return;
    
    if (node->kind == NodeType::Identifier) {
        const auto* id = static_cast<IdentifierNode*>(node);
        unit.add_used_symbol(id->symbol);
    }
    
    // Processar filhos recursivamente
    process_node_for_symbols(node, unit);
}

bool IncrementalChecker::validate_symbol_references(ExecutionUnit& unit) {
    // Verificar se todos os símbolos usados existem e estão válidos na sessão
    for (const auto& symbol_name : unit.get_used_symbols()) {
        // Se o símbolo também é definido nesta unidade, não precisa verificar ainda
        if (unit.get_defined_symbols().find(symbol_name) != unit.get_defined_symbols().end()) {
            continue;
        }
        
        // Verificar se existe na sessão
        if (!session_manager.has_symbol(symbol_name)) {
            // Erro será reportado durante check_node
            // Mas podemos adicionar validação adicional aqui se necessário
        }
    }
    
    return true;
}

void IncrementalChecker::update_dependencies(ExecutionUnit& unit) {
    // Registrar todas as dependências no SessionManager
    for (const auto& symbol_name : unit.get_used_symbols()) {
        // Não registrar dependência se o símbolo é definido nesta mesma unidade
        if (unit.get_defined_symbols().find(symbol_name) == unit.get_defined_symbols().end()) {
            session_manager.add_dependency(unit.get_id(), symbol_name);
        }
    }
}

void IncrementalChecker::process_node_for_symbols(Node* node, ExecutionUnit& unit) {
    if (!node) return;
    
    // Processar filhos recursivamente baseado no tipo do nó
    switch (node->kind) {
        case NodeType::Program: {
            const auto* prog = static_cast<Program*>(node);
            for (const auto& stmt : prog->body) {
                collect_defined_symbols(stmt.get(), unit);
                collect_used_symbols(stmt.get(), unit);
            }
            break;
        }
        case NodeType::BinaryExpression: {
            const auto* bin = static_cast<BinaryExprNode*>(node);
            collect_used_symbols(bin->left.get(), unit);
            collect_used_symbols(bin->right.get(), unit);
            break;
        }
        case NodeType::CallExpression: {
            const auto* call = static_cast<CallExprNode*>(node);
            collect_used_symbols(call->caller.get(), unit);
            for (const auto& arg : call->args) {
                collect_used_symbols(arg.get(), unit);
            }
            break;
        }
        case NodeType::AssignmentExpression: {
            const auto* assign = static_cast<AssignmentExprNode*>(node);
            collect_used_symbols(assign->target.get(), unit);
            collect_used_symbols(assign->value.get(), unit);
            break;
        }
        case NodeType::AccessExpression: {
            const auto* access = static_cast<AccessExprNode*>(node);
            collect_used_symbols(access->expr.get(), unit);
            collect_used_symbols(access->index.get(), unit);
            break;
        }
        case NodeType::MemberExpression: {
            const auto* member = static_cast<MemberExprNode*>(node);
            collect_used_symbols(member->object.get(), unit);
            collect_used_symbols(member->property.get(), unit);
            break;
        }
        case NodeType::ConditionalExpression: {
            const auto* cond = static_cast<ConditionalExprNode*>(node);
            collect_used_symbols(cond->condition.get(), unit);
            collect_used_symbols(cond->true_expr.get(), unit);
            collect_used_symbols(cond->false_expr.get(), unit);
            break;
        }
        case NodeType::LogicalNotExpression: {
            const auto* not_expr = static_cast<LogicalNotExprNode*>(node);
            collect_used_symbols(not_expr->operand.get(), unit);
            break;
        }
        case NodeType::UnaryMinusExpression: {
            const auto* unary = static_cast<UnaryMinusExprNode*>(node);
            collect_used_symbols(unary->operand.get(), unit);
            break;
        }
        case NodeType::IncrementExpression: {
            const auto* inc = static_cast<IncrementExprNode*>(node);
            collect_used_symbols(inc->operand.get(), unit);
            break;
        }
        case NodeType::DecrementExpression: {
            const auto* dec = static_cast<DecrementExprNode*>(node);
            collect_used_symbols(dec->operand.get(), unit);
            break;
        }
        case NodeType::PostIncrementExpression: {
            const auto* post_inc = static_cast<PostIncrementExprNode*>(node);
            collect_used_symbols(post_inc->operand.get(), unit);
            break;
        }
        case NodeType::PostDecrementExpression: {
            const auto* post_dec = static_cast<PostDecrementExprNode*>(node);
            collect_used_symbols(post_dec->operand.get(), unit);
            break;
        }
        case NodeType::ArrayExpression: {
            const auto* arr = static_cast<ArrayExprNode*>(node);
            for (const auto& elem : arr->elements) {
                collect_used_symbols(elem.get(), unit);
            }
            break;
        }
        case NodeType::TupleExpression: {
            const auto* tup = static_cast<TupleExprNode*>(node);
            for (const auto& elem : tup->elements) {
                collect_used_symbols(elem.get(), unit);
            }
            break;
        }
        case NodeType::VectorExpression: {
            const auto* vec = static_cast<VectorExprNode*>(node);
            for (const auto& elem : vec->elements) {
                collect_used_symbols(elem.get(), unit);
            }
            break;
        }
        case NodeType::ListComprehension: {
            const auto* lc = static_cast<ListCompNode*>(node);
            collect_used_symbols(lc->elt.get(), unit);
            for (const auto& [var, iter] : lc->generators) {
                if (var) collect_used_symbols(var.get(), unit);
                collect_used_symbols(iter.get(), unit);
            }
            if (lc->if_cond) collect_used_symbols(lc->if_cond.get(), unit);
            if (lc->else_expr) collect_used_symbols(lc->else_expr.get(), unit);
            break;
        }
        case NodeType::RangeExpression: {
            const auto* range = static_cast<RangeExprNode*>(node);
            collect_used_symbols(range->start.get(), unit);
            collect_used_symbols(range->end.get(), unit);
            break;
        }
        case NodeType::DeclarationStatement: {
            const auto* decl = static_cast<DeclarationStmtNode*>(node);
            collect_used_symbols(decl->target.get(), unit);
            if (decl->value) {
                collect_used_symbols(decl->value.get(), unit);
            }
            break;
        }
        case NodeType::DefStatement: {
            const auto* def = static_cast<DefStmtNode*>(node);
            for (const auto& param : def->parameters) {
                for (const auto& [name, type] : param.parameter) {
                    // Parâmetros são definições locais, não precisam ser coletados como usados
                }
            }
            for (const auto& stmt : def->body) {
                collect_defined_symbols(stmt.get(), unit);
                collect_used_symbols(stmt.get(), unit);
            }
            break;
        }
        case NodeType::IfStatement: {
            const auto* if_stmt = static_cast<IfStatementNode*>(node);
            collect_used_symbols(if_stmt->condition.get(), unit);
            for (const auto& stmt : if_stmt->consequent) {
                collect_defined_symbols(stmt.get(), unit);
                collect_used_symbols(stmt.get(), unit);
            }
            for (const auto& stmt : if_stmt->alternate) {
                collect_defined_symbols(stmt.get(), unit);
                collect_used_symbols(stmt.get(), unit);
            }
            break;
        }
        case NodeType::ForStatement: {
            const auto* for_stmt = static_cast<ForStmtNode*>(node);
            for (const auto& binding : for_stmt->bindings) {
                collect_used_symbols(binding.get(), unit);
            }
            if (for_stmt->range_start) collect_used_symbols(for_stmt->range_start.get(), unit);
            if (for_stmt->range_end) collect_used_symbols(for_stmt->range_end.get(), unit);
            if (for_stmt->iterable) collect_used_symbols(for_stmt->iterable.get(), unit);
            for (const auto& stmt : for_stmt->body) {
                collect_defined_symbols(stmt.get(), unit);
                collect_used_symbols(stmt.get(), unit);
            }
            for (const auto& stmt : for_stmt->else_block) {
                collect_defined_symbols(stmt.get(), unit);
                collect_used_symbols(stmt.get(), unit);
            }
            break;
        }
        case NodeType::WhileStatement: {
            const auto* while_stmt = static_cast<WhileStmtNode*>(node);
            collect_used_symbols(while_stmt->condition.get(), unit);
            for (const auto& stmt : while_stmt->body) {
                collect_defined_symbols(stmt.get(), unit);
                collect_used_symbols(stmt.get(), unit);
            }
            break;
        }
        case NodeType::LoopStatement: {
            const auto* loop_stmt = static_cast<LoopStmtNode*>(node);
            for (const auto& stmt : loop_stmt->body) {
                collect_defined_symbols(stmt.get(), unit);
                collect_used_symbols(stmt.get(), unit);
            }
            break;
        }
        case NodeType::MatchStatement: {
            const auto* match_stmt = static_cast<MatchStmtNode*>(node);
            collect_used_symbols(match_stmt->target.get(), unit);
            for (const auto& case_expr : match_stmt->cases) {
                collect_used_symbols(case_expr.get(), unit);
            }
            for (const auto& body : match_stmt->bodies) {
                for (const auto& stmt : body) {
                    collect_defined_symbols(stmt.get(), unit);
                    collect_used_symbols(stmt.get(), unit);
                }
            }
            break;
        }
        case NodeType::ReturnStatement: {
            const auto* ret_stmt = static_cast<ReturnStmtNode*>(node);
            if (ret_stmt->value) {
                collect_used_symbols(ret_stmt->value.get(), unit);
            }
            break;
        }
        case NodeType::Identifier:
        case NodeType::NumericLiteral:
        case NodeType::StringLiteral:
        case NodeType::BooleanLiteral:
        case NodeType::BreakStatement:
        case NodeType::ContinueStatement:
            // Nós folha - não têm filhos para processar
            break;
        default:
            // Para outros tipos, não fazer nada (pode ser expandido no futuro)
            break;
    }
}

} // namespace interactive
} // namespace nv
