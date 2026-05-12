#include "frontend/checker/statements/check_program.hpp"
#include "frontend/checker/statements/check_class_stmt.hpp"
#include "frontend/checker/statements/check_enum_stmt.hpp"
#include "frontend/checker/statements/check_interface_stmt.hpp"
#include "frontend/checker/statements/check_import_stmt.hpp"
#include "frontend/checker/statements/check_function_stmt.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/statements/if_statement_node.hpp"
#include "frontend/ast/statements/for_stmt_node.hpp"
#include "frontend/ast/statements/while_stmt_node.hpp"
#include "frontend/ast/statements/forever_stmt_node.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
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
            return nullptr;
        }

        auto* id_node = static_cast<IdentifierNode*>(assign_node->target.get());
        
        // Verificar se o identifier já existe no escopo atual
        if (identifier_exists(checker, id_node->symbol)) {
            return nullptr;
        }

        // Criar DeclarationStmtNode
        auto decl_node = std::make_unique<DeclarationStmtNode>(
            std::unique_ptr<Expr>(static_cast<Expr*>(id_node->clone())),
            assign_node->value 
                ? std::unique_ptr<Expr>(static_cast<Expr*>(assign_node->value->clone()))
                : nullptr,
            "automatic",  // tipo automático para inferência
            false         // não constante (mutável)
        );

        // Copiar posição do assignment para a declaração
        if (assign_node->position) {
            decl_node->position = std::make_unique<PositionData>(*assign_node->position);
        }

        // Registrar o símbolo no escopo do checker para que assignments
        // subsequentes (x += 5) reconheçam que x já existe
        checker->scope->put_key(
            id_node->symbol,
            std::make_shared<nv::TypeVar>(checker->unify_ctx.get_next_var_id()),
            true  // mutable = true
        );

        return decl_node;
    }

    // Processa recursivamente uma lista de nodes (ex: or block stmts)
    void process_nodelist(std::vector<std::unique_ptr<Node>>& nodes, nv::Checker* checker) {
        for (size_t i = 0; i < nodes.size(); i++) {
            auto& stmt = nodes[i];
            if (!stmt) continue;

            // Converter AssignmentExpression para DeclarationStmtNode se necessário
            if (stmt->kind == NodeType::AssignmentExpression) {
                auto* assign_node = static_cast<AssignmentExprNode*>(stmt.get());
                auto converted = convert_assignment_to_declaration(assign_node, checker);
                if (converted) {
                    stmt.reset(converted.release());
                }
            }
            
            // Recursão para blocos aninhados
            if (stmt->kind == NodeType::OrExpression) {
                auto* or_node = static_cast<OrExprNode*>(stmt.get());
                std::vector<std::unique_ptr<Node>> stmt_nodes;
                for (auto& stmt : or_node->block_stmts) {
                    if (stmt) {
                        stmt_nodes.push_back(std::unique_ptr<Node>(static_cast<Node*>(stmt.release())));
                    }
                }
                process_nodelist(stmt_nodes, checker);
                // Converter de volta
                or_node->block_stmts.clear();
                for (auto& node : stmt_nodes) {
                    if (node) {
                        or_node->block_stmts.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(node.release())));
                    }
                }
            }
        }
    }

    // Processa recursivamente um CodeBlock convertendo assignments não declarados
    // em declarações
    void process_codeblock(CodeBlock& body, nv::Checker* checker) {
        for (size_t i = 0; i < body.size(); i++) {
            auto& stmt = body[i];
            
            // Verificar se é AssignmentExpression
            if (stmt->kind == NodeType::AssignmentExpression) {
                auto* assign_node = static_cast<AssignmentExprNode*>(stmt.get());
                auto converted = convert_assignment_to_declaration(assign_node, checker);
                if (converted) {
                    stmt.reset(converted.release());
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
                case NodeType::FunctionStatement: {
                    // Corpos de função são processados em check_function_stmt com escopo correto
                    // Não processar aqui para evitar verificação no escopo errado
                    continue;
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
                    std::vector<std::unique_ptr<Node>> stmt_nodes;
                    for (auto& stmt : or_node->block_stmts) {
                        if (stmt) {
                            stmt_nodes.push_back(std::unique_ptr<Node>(static_cast<Node*>(stmt.release())));
                        }
                    }
                    process_nodelist(stmt_nodes, checker);
                    // Converter de volta
                    or_node->block_stmts.clear();
                    for (auto& node : stmt_nodes) {
                        if (node) {
                            or_node->block_stmts.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(node.release())));
                        }
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }

} // anonymous namespace

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

    // Terceira passagem: Registrar interfaces primeiro (antes das classes).
    for (auto& el : program->body) {
        if (el->kind == NodeType::InterfaceStatement) {
            check_interface_stmt(ch, el.get());
        }
    }

    // Quarta passagem: Registrar todas as classes e enums.
    // Isso permite usar classes/enums como tipos em parâmetros/retornos de funções
    // e em declarações mesmo quando aparecem depois no arquivo.
    for (auto& el : program->body) {
        if (el->kind == NodeType::ClassStatement) {
            check_class_stmt(ch, el.get());
        } else if (el->kind == NodeType::EnumStatement) {
            check_enum_stmt(ch, el.get());
        }
    }

    // Quinta passagem: Registrar assinaturas de todas as funções (defs) antes de checar corpos
    for (auto& el : program->body) {
        if (el->kind == NodeType::FunctionStatement) {
            auto* function_stmt = static_cast<FunctionStmtNode*>(el.get());
            
            // Processar parâmetros para obter o tipo da função
            std::vector<std::shared_ptr<nv::Type>> param_types;
            for (const auto& param : function_stmt->parameters) {
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
            if (function_stmt->return_type.empty() || function_stmt->return_type == "automatic") {
                return_type = std::make_shared<nv::TypeVar>(ch->unify_ctx.get_next_var_id());
            } else {
                return_type = ch->gettyptr(function_stmt->return_type);
            }
            
            auto func_type = std::make_shared<nv::Function>(param_types, return_type);
            
            // Registrar função no escopo SEM checar o corpo ainda
            // Passamos false para não travar o símbolo
            ch->scope->put_key(function_stmt->name, func_type, false);
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
