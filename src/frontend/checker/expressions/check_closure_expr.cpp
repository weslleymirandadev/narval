#include "frontend/checker/checker.hpp"
#include "frontend/ast/expressions/closure_expr_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/call_expr_node.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "frontend/ast/expressions/unary_minus_expr_node.hpp"
#include "frontend/ast/expressions/access_expr_node.hpp"
#include "frontend/ast/expressions/array_expr_node.hpp"
#include "frontend/ast/expressions/vector_expr_node.hpp"
#include "frontend/ast/expressions/conditional_expr_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/statements/return_stmt_node.hpp"
#include "frontend/ast/statements/if_statement_node.hpp"
#include "frontend/ast/statements/while_stmt_node.hpp"
#include <unordered_set>

// Coleta todas as variáveis livres (não-locais) usadas no corpo da closure
static void collect_free_variables(const CodeBlock& body, 
                                   const std::vector<std::pair<std::string, std::string>>& params,
                                   std::unordered_set<std::string>& local_vars,
                                   std::unordered_set<std::string>& free_vars,
                                   nv::Checker& ch) {
    std::unordered_set<std::string> param_names;
    for (const auto& p : params) {
        param_names.insert(p.first);
    }
    
    std::function<void(Node*)> visit = [&](Node* node) {
        if (!node) return;
        
        // Coletar identificadores
        if (node->kind == NodeType::Identifier) {
            auto* id = static_cast<IdentifierNode*>(node);
            if (param_names.count(id->symbol) == 0 && local_vars.count(id->symbol) == 0) {
                // Tentar encontrar no escopo externo
                try {
                    ch.scope->get_key(id->symbol);
                    free_vars.insert(id->symbol);
                } catch (...) {
                    // Não encontrado nem localmente nem externamente
                }
            }
            return;
        }

        // Coletar declarações/atribuições locais e visitar seus filhos.
        if (node->kind == NodeType::AssignmentExpression) {
            auto* assign = static_cast<AssignmentExprNode*>(node);
            if (assign->value) visit(assign->value.get());

            if (assign->target && assign->target->kind == NodeType::Identifier) {
                auto* target_id = static_cast<IdentifierNode*>(assign->target.get());
                bool is_outer = false;
                try {
                    ch.scope->get_key(target_id->symbol);
                    is_outer = true;
                } catch (...) {}

                if (param_names.count(target_id->symbol) == 0 && !local_vars.count(target_id->symbol)) {
                    if (is_outer) {
                        free_vars.insert(target_id->symbol);
                    } else {
                        local_vars.insert(target_id->symbol);
                    }
                }
            } else if (assign->target) {
                visit(assign->target.get());
            }
            return;
        }
        if (node->kind == NodeType::DeclarationStatement) {
            auto* decl = static_cast<DeclarationStmtNode*>(node);
            if (decl->value) visit(decl->value.get());
            if (decl->target && decl->target->kind == NodeType::Identifier) {
                local_vars.insert(static_cast<IdentifierNode*>(decl->target.get())->symbol);
            }
            return;
        }
        
        // Recursivamente visitar filhos
        if (auto* expr = dynamic_cast<Expr*>(node)) {
            if (auto* call = dynamic_cast<CallExprNode*>(expr)) {
                visit(call->caller.get());
                for (const auto& arg : call->args) {
                    visit(arg->value.get());
                }
            }
            if (auto* binop = dynamic_cast<BinaryExprNode*>(expr)) {
                visit(binop->left.get());
                visit(binop->right.get());
            }
            if (auto* unary = dynamic_cast<UnaryMinusExprNode*>(expr)) {
                visit(unary->operand.get());
            }
            if (auto* access = dynamic_cast<AccessExprNode*>(expr)) {
                visit(access->expr.get());
                visit(access->index.get());
            }
            if (auto* arr = dynamic_cast<ArrayExprNode*>(expr)) {
                for (const auto& element : arr->elements) visit(element.get());
            }
            if (auto* vec = dynamic_cast<VectorExprNode*>(expr)) {
                for (const auto& element : vec->elements) visit(element.get());
            }
            if (auto* cond = dynamic_cast<ConditionalExprNode*>(expr)) {
                visit(cond->condition.get());
                visit(cond->true_expr.get());
                visit(cond->false_expr.get());
            }
            if (auto* member = dynamic_cast<MemberExprNode*>(expr)) {
                visit(member->object.get());
            }
        }
        if (auto* stmt = dynamic_cast<Stmt*>(node)) {
            if (auto* ret = dynamic_cast<ReturnStmtNode*>(stmt)) {
                if (ret->value) visit(ret->value.get());
            }
            if (auto* if_stmt = dynamic_cast<IfStatementNode*>(stmt)) {
                visit(if_stmt->condition.get());
                for (const auto& child : if_stmt->consequent) visit(child.get());
                for (const auto& child : if_stmt->alternate) visit(child.get());
            }
            if (auto* while_stmt = dynamic_cast<WhileStmtNode*>(stmt)) {
                visit(while_stmt->condition.get());
                for (const auto& child : while_stmt->body) visit(child.get());
            }
        }
    };
    
    for (const auto& stmt : body) {
        visit(stmt.get());
    }
}

// Mirrors the logic in check_program.cpp:process_codeblock but scoped to the
static void closure_prepass(CodeBlock& body, nv::Checker& ch) {
    for (auto& stmt : body) {
        if (!stmt || stmt->kind != NodeType::AssignmentExpression) continue;
        auto* assign = static_cast<AssignmentExprNode*>(stmt.get());
        if (assign->op != "=" || !assign->target ||
            assign->target->kind != NodeType::Identifier) continue;

        auto* id = static_cast<IdentifierNode*>(assign->target.get());
        bool exists = false;
        try { ch.scope->get_key(id->symbol); exists = true; } catch (...) {}
        if (exists) continue;
        // Convert to declaration and pre-register with a fresh type variable
        auto decl = std::make_unique<DeclarationStmtNode>(
            std::unique_ptr<Expr>(static_cast<Expr*>(id->clone())),
            assign->value
                ? std::unique_ptr<Expr>(static_cast<Expr*>(assign->value->clone()))
                : nullptr,
            "automatic",
            false
        );
        if (assign->position)
            decl->position = std::make_unique<PositionData>(*assign->position);

        // Registrar como mutable (true) para que check_decl_stmt possa
        // sobrescrever com o tipo real inferido na passagem final
        ch.scope->put_key(
            id->symbol,
            std::make_shared<nv::TypeVar>(ch.unify_ctx.get_next_var_id()),
            true  // mutable = true
        );
        stmt = std::move(decl);
    }
}

namespace nv {

std::shared_ptr<Type> check_closure_expr(ClosureExprNode* node, Checker& ch) {
    if (node->type_checked && node->cached_type) {
        return node->cached_type;
    }

    std::vector<std::shared_ptr<nv::Type>> param_types;

    // PRIMEIRA PASSAGEM: Detectar variáveis capturadas (antes de entrar no escopo)
    auto saved_outer_scope = ch.scope;
    std::unordered_set<std::string> local_vars;
    std::unordered_set<std::string> free_vars;
    
    // Coletar variáveis livres usando o escopo EXTERNO
    collect_free_variables(node->body, node->parameters, local_vars, free_vars, ch);
    
    // Converter variáveis livres para o vetor de capturas
    node->captures.clear();
    for (const auto& var : free_vars) {
        node->captures.push_back(var);
    }

    // SEGUNDA PASSAGEM: Entrar no escopo e processar o tipo
    ch.push_scope();
    auto saved_return_type = ch.current_return_type;

    // Adicionar parâmetros ao escopo atual
    for (const auto& param : node->parameters) {
        // Parâmetros são obrigatoriamente tipados
        std::string type_str = param.second;
        auto param_type = ch.gettyptr(type_str);
        param_types.push_back(param_type);
        ch.scope->put_key(param.first, param_type, true);
        ch.closure_fallback_symbols[param.first] = param_type;
    }

    // Registrar variáveis capturadas no escopo da closure
    for (const auto& capture_name : node->captures) {
        try {
            auto capture_type = saved_outer_scope->get_key(capture_name);
            // Registrar no escopo da closure com o mesmo tipo
            ch.scope->put_key(capture_name, capture_type, true);
            ch.closure_fallback_symbols[capture_name] = capture_type;
        } catch (...) {
            // Se não encontrar, usar um type var
            auto capture_type = ch.unify_ctx.new_type_var();
            ch.scope->put_key(capture_name, capture_type, true);
            ch.closure_fallback_symbols[capture_name] = capture_type;
        }
    }

    for (const auto& local_name : local_vars) {
        if (!ch.closure_fallback_symbols.count(local_name)) {
            ch.closure_fallback_symbols[local_name] = ch.unify_ctx.new_type_var();
        }
    }

    // Convert undeclared assignments in body to declarations (like check_program does)
    closure_prepass(node->body, ch);

    // Permissive return type so ReturnStmtNode validation doesn't false-positive
    ch.current_return_type = ch.unify_ctx.new_type_var();

    std::shared_ptr<nv::Type> return_type = ch.gettyptr("void");
    for (const auto& stmt : node->body) {
        if (stmt) {
            auto t = ch.check_node(stmt.get());
            if (t) return_type = t;
        }
    }

    // Usar tipo de retorno especificado na closure (ou inferido se auto)
    auto closure_return_type = (node->return_type == "auto")
        ? ch.unify_ctx.new_type_var()
        : ch.gettyptr(node->return_type);
    
    // Verificar se o tipo de retorno inferido é compatível com o declarado
    if (return_type && return_type->kind != nv::Kind::VOID) {
        try {
            ch.unify_ctx.unify(return_type, closure_return_type);
        } catch (std::runtime_error& e) {
            ch.error(node, "Closure return type mismatch: " + std::string(e.what()));
        }
    }
    
    // Se o tipo de retorno é uma função (closure aninhada), aceitar como válido
    // if (closure_return_type->kind == nv::Kind::FUNCTION) {
    //     Closures que retornam closures são válidas
    //     Aceitar sem verificação de unificação
    //     return function_type;
    // }

    // Salvar referência ao escopo pai antes do pop_scope
    auto parent_scope = ch.scope;
    
    ch.pop_scope();
    ch.current_return_type = saved_return_type;

    // NÃO registrar parâmetros no escopo pai para evitar conflitos em closures aninhadas
    // Isso causa problemas com parâmetros de closures internas

    auto function_type = std::make_shared<Function>(param_types, closure_return_type);
    
    // Registrar o tipo função para que possa ser usado como parâmetro em outras closures
    std::string type_name = create_function_type_string(node->parameters, node->return_type);
    ch.scope->put_key(type_name, function_type, false);

    node->type_checked = true;
    node->cached_type = function_type;
    
    return function_type;
}
}
