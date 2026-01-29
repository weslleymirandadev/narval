#include "frontend/checker/statements/check_program.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/statements/if_statement_node.hpp"
#include "frontend/ast/statements/for_stmt_node.hpp"
#include "frontend/ast/statements/while_stmt_node.hpp"
#include "frontend/ast/statements/loop_stmt_node.hpp"
#include "frontend/ast/statements/label_stmt_node.hpp"
#include "frontend/ast/statements/match_stmt_node.hpp"
#include <stdexcept>

namespace {
    // Helper: verifica se um identifier existe no escopo atual ou em escopos pais
    bool identifier_exists(rph::Checker* checker, const std::string& symbol) {
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
        rph::Checker* checker
    ) {
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
    
    // Processa recursivamente um CodeBlock convertendo assignments não declarados
    void process_codeblock(CodeBlock& body, rph::Checker* checker) {
        for (size_t i = 0; i < body.size(); i++) {
            auto& stmt = body[i];
            
            // Verificar se é AssignmentExpression
            if (stmt->kind == NodeType::AssignmentExpression) {
                auto* assign_node = static_cast<AssignmentExprNode*>(stmt.get());
                
                // Tentar converter para declaração
                auto converted = convert_assignment_to_declaration(assign_node, checker);
                
                if (converted) {
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
                case NodeType::LoopStatement: {
                    auto* loop_stmt = static_cast<LoopStmtNode*>(stmt.get());
                    process_codeblock(loop_stmt->body, checker);
                    break;
                }
                case NodeType::LabelStatement: {
                    auto* label_stmt = static_cast<LabelStmtNode*>(stmt.get());
                    process_codeblock(label_stmt->body, checker);
                    break;
                }
                case NodeType::MatchStatement: {
                    auto* match_stmt = static_cast<MatchStmtNode*>(stmt.get());
                    for (auto& case_body : match_stmt->bodies) {
                        process_codeblock(case_body, checker);
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }
}

std::shared_ptr<rph::Type>& check_program_stmt(rph::Checker* ch, Node* node) {
    auto* program = static_cast<Program*>(node);

    // Primeira passagem: converter AssignmentExpression não declarados em declarações
    // Processa recursivamente todos os blocos de código (incluindo aninhados)
    process_codeblock(program->body, ch);

    // Segunda passagem: processar todos os statements (incluindo os convertidos)
    for (auto& el : program->body) {
        try {
            ch->check_node(el.get());
        } catch (std::runtime_error e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }

    return ch->gettyptr("void");
}