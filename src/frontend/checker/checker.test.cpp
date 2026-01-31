#include <iostream>
#include <fstream>
#include <string>
#include "frontend/lexer/lexer.hpp"
#include "frontend/lexer/token.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/checker/checker_meth.hpp"
#include "frontend/module_manager.hpp"
#include "frontend/ast/ast.hpp"

int main(int argc, char* argv[]) {
    std::string filename = argc > 1 ? argv[1] : "../test/main.nv";
    std::string module_name = "main";

    ModuleManager module_manager;
    try {
        std::cout << "========================================\n";
        std::cout << "Teste de Análise Semântica\n";
        std::cout << "Arquivo: " << filename << "\n";
        std::cout << "========================================\n\n";
        
        module_manager.compile_module(module_name, filename, ENABLE_PARSE | ENABLE_CHECKING);

        const auto& modules = module_manager.get_modules();
        
        // Criar checker para análise detalhada
        nv::Checker checker;
        checker.set_source_file(filename);
        
        std::cout << "Verificando tipos inferidos...\n\n";
        
        for (const auto& [name, module] : modules) {
            std::cout << "Módulo: " << name << "\n";
            std::cout << "----------------------------------------\n";
            
            if (module.ast) {
                // Executar verificação completa primeiro
                try {
                    checker.check_node(module.ast.get());
                } catch (const std::exception& e) {
                    std::cerr << "Erro na verificação: " << e.what() << "\n";
                }
                
                Program* program = dynamic_cast<Program*>(module.ast.get());
                if (program) {
                    int stmt_num = 0;
                    for (const auto& stmt : program->body) {
                        stmt_num++;
                        try {
                            // Tentar inferir tipo se for expressão ou declaração
                            std::shared_ptr<nv::Type> inferred_type = nullptr;
                            if (stmt->kind == NodeType::DeclarationStatement) {
                                auto* decl = static_cast<DeclarationStmtNode*>(stmt.get());
                                // Para declarações, obter tipo do namespace após verificação
                                try {
                                    auto* id = static_cast<IdentifierNode*>(decl->target.get());
                                    auto& type = checker.scope->get_key(id->symbol);
                                    // Resolver tipo (pode ser polimórfico ou ter variáveis de tipo)
                                    inferred_type = checker.unify_ctx.resolve(type);
                                    // Se ainda for polimórfico após resolução, tentar instanciar
                                    if (inferred_type && inferred_type->kind == nv::Kind::POLY_TYPE) {
                                        auto poly = std::static_pointer_cast<nv::PolyType>(inferred_type);
                                        int next_id = checker.unify_ctx.get_next_var_id();
                                        inferred_type = poly->instantiate(next_id);
                                        inferred_type = checker.unify_ctx.resolve(inferred_type);
                                    }
                                    // Se o tipo resolvido for Void, tentar inferir do value diretamente
                                    if (!inferred_type || inferred_type->kind == nv::Kind::VOID) {
                                        if (decl->value) {
                                            inferred_type = checker.infer_expr(decl->value.get());
                                            inferred_type = checker.unify_ctx.resolve(inferred_type);
                                        }
                                    }
                                } catch (...) {
                                    // Se não encontrar no namespace, inferir do value
                                    if (decl->value) {
                                        inferred_type = checker.infer_expr(decl->value.get());
                                        inferred_type = checker.unify_ctx.resolve(inferred_type);
                                    }
                                }
                            } else if (stmt->kind == NodeType::BinaryExpression || 
                                      stmt->kind == NodeType::CallExpression ||
                                      stmt->kind == NodeType::ArrayExpression ||
                                      stmt->kind == NodeType::VectorExpression ||
                                      stmt->kind == NodeType::TupleExpression ||
                                      stmt->kind == NodeType::AssignmentExpression) {
                                inferred_type = checker.infer_expr(stmt.get());
                                inferred_type = checker.unify_ctx.resolve(inferred_type);
                            }
                            
                            std::cout << "  Statement " << stmt_num << ": ";
                            
                            // Mostrar tipo inferido se disponível
                            if (inferred_type) {
                                std::cout << "tipo inferido = " << inferred_type->toString();
                            } else {
                                // Verificar tipo tradicional
                                auto& type = checker.check_node(stmt.get());
                                std::cout << "tipo verificado = " << type->toString();
                            }
                            std::cout << "\n";
                            
                        } catch (const std::exception& e) {
                            std::cout << "  Statement " << stmt_num << ": ERRO - " << e.what() << "\n";
                        }
                    }
                }
            }
            std::cout << "\n";
        }
        
        std::cout << "========================================\n";
        std::cout << "Estatísticas do Checker:\n";
        std::cout << "  Próximo ID de variável de tipo: " << checker.unify_ctx.get_next_var_id() << "\n";
        std::cout << "  Erros encontrados: " << (checker.err ? "SIM" : "NÃO") << "\n";
        std::cout << "========================================\n";
        
    } catch (const std::exception& e) {
        std::cerr << "\nERRO durante teste de análise semântica: " << e.what() << "\n";
        return 1;
    }

    std::cout << "\n✓ Teste de análise semântica concluído com sucesso!\n";
    return 0;
}
