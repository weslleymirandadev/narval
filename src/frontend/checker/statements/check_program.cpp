#include "frontend/checker/statements/check_program.hpp"
#include "frontend/checker/statements/check_class_def.hpp"
#include "frontend/checker/statements/check_enum_def.hpp"
#include "frontend/checker/statements/check_interface_def.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/statements/if_statement_node.hpp"
#include "frontend/ast/statements/for_stmt_node.hpp"
#include "frontend/ast/statements/while_stmt_node.hpp"
#include "frontend/ast/statements/forever_stmt_node.hpp"
#include "frontend/ast/statements/def_stmt_node.hpp"
#include "frontend/ast/statements/match_stmt_node.hpp"
#include "frontend/ast/expressions/or_expr_node.hpp"
#include <stdexcept>

namespace {
    // Helper: verifica se um identifier existe no escopo atual ou em escopos pais
    bool identifier_exists(nv::Checker* checker, const std::string& symbol) {
        try {
            checker->scope->get_key(symbol);
            return true;
        } catch (std::runtime_error&) {
            return false;
        }
    }
    
    // Converte AssignmentExpression em DeclarationStmtNode quando o identifier não existe
    std::unique_ptr<Stmt> convert_assignment_to_declaration(
        AssignmentExprNode* assign_node,
        nv::Checker* checker
    ) {
        // Operadores compostos (+=, -=, etc.) nunca são declarações
        if (assign_node->op != "=") {
            return nullptr;
        }

        // Verificar se o target é um Identifier
        if (assign_node->target->kind != NodeType::Identifier) {
            // Não é um identifier simples, não converter
            return nullptr;
        }
        
        auto* id_node = static_cast<IdentifierNode*>(assign_node->target.get());
        const std::string& symbol = id_node->symbol;
        
        // Verificar se o identifier já existe
        if (identifier_exists(checker, symbol)) {
            // Identifier já existe, manter como assignment
            return nullptr;
        }
        
        // Identifier não existe - converter para declaração mutável com inferência automática
        // Criar novo IdentifierNode para o target (clonar)
        auto new_target = std::unique_ptr<Expr>(static_cast<Expr*>(id_node->clone()));
        
        // Clonar o value
        auto new_value = assign_node->value ? 
            std::unique_ptr<Expr>(static_cast<Expr*>(assign_node->value->clone())) : nullptr;
        
        // Criar DeclarationStmtNode com tipo "automatic" (inferência automática) e não constante (mutável)
        auto decl_node = std::make_unique<DeclarationStmtNode>(
            std::move(new_target),
            std::move(new_value),
            "automatic",  // tipo automático para inferência
            false         // não constante (mutável)
        );
        
        // Copiar posição do assignment para a declaração
        if (assign_node->position) {
            decl_node->position = std::make_unique<PositionData>(*assign_node->position);
        }
        
        return decl_node;
    }
    
    // Processa recursivamente uma lista de nodes (ex: or block stmts)
    void process_nodelist(std::vector<std::unique_ptr<Node>>& nodes, nv::Checker* checker) {
        for (size_t i = 0; i < nodes.size(); i++) {
            auto& stmt = nodes[i];
            if (!stmt) continue;
            if (stmt->kind == NodeType::AssignmentExpression) {
                auto* assign_node = static_cast<AssignmentExprNode*>(stmt.get());
                auto converted = convert_assignment_to_declaration(assign_node, checker);
                if (converted) {
                    stmt.reset(converted.release());
                }
            }
            // Recurse into or blocks nested inside
            if (stmt->kind == NodeType::OrExpression) {
                auto* or_node = static_cast<OrExprNode*>(stmt.get());
                process_nodelist(or_node->block_stmts, checker);
            }
        }
    }

    // Processa recursivamente um CodeBlock convertendo assignments não declarados
    void process_codeblock(CodeBlock& body, nv::Checker* checker) {
        for (size_t i = 0; i < body.size(); i++) {
            auto& stmt = body[i];
            
            // Verificar se é AssignmentExpression
            if (stmt->kind == NodeType::AssignmentExpression) {
                auto* assign_node = static_cast<AssignmentExprNode*>(stmt.get());
                
                // Tentar converter para declaração
                auto converted = convert_assignment_to_declaration(assign_node, checker);
                
                if (converted) {
                    // Registrar o símbolo no scope do checker para que assignemnts
                    // subsequentes (x += 5) reconheçam que x já existe
                    auto* id_node = static_cast<IdentifierNode*>(assign_node->target.get());
                    // Registrar como mutable (true) para que check_decl_stmt possa
                    // sobrescrever com o tipo real inferido na passagem final
                    checker->scope->put_key(
                        id_node->symbol,
                        std::make_shared<nv::TypeVar>(checker->unify_ctx.get_next_var_id()),
                        true
                    );
                    // Substituir o assignment pela declaração
                    stmt = std::move(converted);
                }
            }
            
            // Processar recursivamente blocos aninhados
            switch (stmt->kind) {
                case NodeType::IfStatement: {
                    auto* if_stmt = static_cast<IfStatementNode*>(stmt.get());
                    process_codeblock(if_stmt->consequent, checker);
                    process_codeblock(if_stmt->alternate, checker);
                    break;
                }
                case NodeType::ForStatement: {
                    auto* for_stmt = static_cast<ForStmtNode*>(stmt.get());
                    process_codeblock(for_stmt->body, checker);
                    process_codeblock(for_stmt->else_block, checker);
                    break;
                }
                case NodeType::WhileStatement: {
                    auto* while_stmt = static_cast<WhileStmtNode*>(stmt.get());
                    process_codeblock(while_stmt->body, checker);
                    break;
                }
                case NodeType::ForeverStatement: {
                    auto* forever_stmt = static_cast<ForeverStmtNode*>(stmt.get());
                    process_codeblock(forever_stmt->body, checker);
                    break;
                }
                case NodeType::DefStatement: {
                    auto* def_stmt = static_cast<DefStmtNode*>(stmt.get());
                    process_codeblock(def_stmt->body, checker);
                    break;
                }
                case NodeType::MatchStatement: {
                    auto* match_stmt = static_cast<MatchStmtNode*>(stmt.get());
                    for (auto& case_body : match_stmt->bodies) {
                        process_codeblock(case_body, checker);
                    }
                    break;
                }
                case NodeType::OrExpression: {
                    auto* or_node = static_cast<OrExprNode*>(stmt.get());
                    process_nodelist(or_node->block_stmts, checker);
                    break;
                }
                default:
                    break;
            }
        }
    }
}

std::shared_ptr<nv::Type>& check_program_stmt(nv::Checker* ch, Node* node) {
    auto* program = static_cast<Program*>(node);

    // Primeira passagem: processar imports ANTES de tudo para registrar símbolos no escopo
    for (auto& el : program->body) {
        if (el->kind == NodeType::ImportStatement) {
            ch->check_node(el.get());
        }
    }

    // Segunda passagem: converter AssignmentExpression não declarados em declarações
    // Processa recursivamente todos os blocos de código (incluindo aninhados)
    process_codeblock(program->body, ch);

    // TERCEIRA PASSAGEM [NOVA]: Registrar interfaces primeiro (antes das classes).
    for (auto& el : program->body) {
        if (el->kind == NodeType::InterfaceDef) {
            check_interface_def(ch, el.get());
        }
    }

    // QUARTA PASSAGEM: Registrar todas as classes e enums.
    // Isso permite usar classes/enums como tipos em parâmetros/retornos de funções
    // e em declarações mesmo quando aparecem depois no arquivo.
    for (auto& el : program->body) {
        if (el->kind == NodeType::ClassDef) {
            check_class_def(ch, el.get());
        } else if (el->kind == NodeType::EnumDef) {
            check_enum_def(ch, el.get());
        }
    }

    // QUARTA PASSAGEM [NOVA]: Registrar assinaturas de todas as funções (defs) antes de checar corpos
    for (auto& el : program->body) {
        if (el->kind == NodeType::DefStatement) {
            auto* def_stmt = static_cast<DefStmtNode*>(el.get());
            
            // Processar parâmetros para obter o tipo da função
            std::vector<std::shared_ptr<nv::Type>> param_types;
            for (const auto& param : def_stmt->parameters) {
                std::string param_name;
                std::string param_type_str;
                for (const auto& [key, value] : param.parameter) {
                    param_name = key;
                    param_type_str = value;
                }
                
                std::shared_ptr<nv::Type> param_type;
                if (param_type_str.empty() || param_type_str == "automatic") {
                    param_type = std::make_shared<nv::TypeVar>(ch->unify_ctx.get_next_var_id());
                } else {
                    param_type = ch->gettyptr(param_type_str);
                }
                param_types.push_back(param_type);
            }
            
            std::shared_ptr<nv::Type> return_type;
            if (def_stmt->return_type.empty() || def_stmt->return_type == "automatic") {
                return_type = std::make_shared<nv::TypeVar>(ch->unify_ctx.get_next_var_id());
            } else {
                return_type = ch->gettyptr(def_stmt->return_type);
            }
            
            auto func_type = std::make_shared<nv::Def>(param_types, return_type);
            
            // Registrar função no escopo SEM checar o corpo ainda
            // Passamos false para não travar o símbolo
            ch->scope->put_key(def_stmt->name, func_type, false);
        }
    }

    // Passagem final: processar todos os statements restantes (incluindo os convertidos)
    for (auto& el : program->body) {
        if (el->kind != NodeType::ImportStatement) {
            ch->check_node(el.get());
        }
    }

    return ch->gettyptr("void");
}