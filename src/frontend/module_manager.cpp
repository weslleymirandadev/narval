#include "frontend/module_manager.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/checker/checker_meth.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <regex>
#include <filesystem>
#include <functional>

std::string ModuleManager::read_file(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Erro ao abrir o arquivo: " + file_path);
    }
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    return source;
}

void ModuleManager::load_module(const std::string& module_name, const std::string& file_path, int config) {
    if (modules.find(module_name) != modules.end()) return;

    std::string source = read_file(file_path);
    Module module;
    module.source = source;
    module.directory = std::filesystem::path(file_path).parent_path().string();

    Lexer lexer(source, file_path);
    module.tokens = lexer.tokenize();
    module.dependencies = lexer.get_imported_modules();
    module.import_infos = lexer.get_import_infos();
    module.name = lexer.get_module_name();

    if (config && ENABLE_PARSE == ENABLE_PARSE) {
        Parser parser;
        module.ast = parser.produce_ast(module.tokens, module.import_infos);
    }
    if (config && ENABLE_CHECKING == ENABLE_CHECKING) {
        nv::Checker checker;
        checker.set_source_file(file_path);
        checker.check_node(module.ast.get());
    }
    modules[module.name] = std::move(module);
}

void ModuleManager::resolve_dependencies(const std::string& module_name, const std::string& file_path, int config) {
    if (visited.find(module_name) != visited.end()) {
        throw std::runtime_error("Erro: Ciclo de importação detectado com o módulo " + module_name);
    }

    visited.insert(module_name);
    load_module(module_name, file_path, config);

    auto& module = modules[module_name];
    // Usa import_infos para resolver dependências (nova sintaxe)
    for (const auto& import_info : module.import_infos) {
        std::string clean_dep = std::regex_replace(import_info.module_path, std::regex("\""), "");
        std::string dep_path = (std::filesystem::path(module.directory) / (clean_dep)).string();
        if (!std::ifstream(dep_path).good()) {
            throw std::runtime_error("Módulo " + import_info.module_path + " não encontrado");
        }
        resolve_dependencies(clean_dep, dep_path, config);
    }
    // Mantém compatibilidade com código antigo usando dependencies
    for (const auto& dep : module.dependencies) {
        // Verifica se já foi processado via import_infos
        bool already_processed = false;
        for (const auto& import_info : module.import_infos) {
            std::string clean_import = std::regex_replace(import_info.module_path, std::regex("\""), "");
            if (clean_import == dep) {
                already_processed = true;
                break;
            }
        }
        if (!already_processed) {
            std::string clean_dep = std::regex_replace(dep, std::regex("\""), "");
            std::string dep_path = (std::filesystem::path(module.directory) / (clean_dep)).string();
            if (!std::ifstream(dep_path).good()) {
                throw std::runtime_error("Módulo " + dep + " não encontrado");
            }
            resolve_dependencies(clean_dep, dep_path, config);
        }
    }

    visited.erase(module_name);
}

const std::map<std::string, ModuleManager::Module>& ModuleManager::get_modules() const {
    return modules;
}

void ModuleManager::compile_module(const std::string& module_name, const std::string& file_path, int config) {
    resolve_dependencies(module_name, file_path, config);
}

std::unique_ptr<Node> ModuleManager::get_combined_ast(const std::string& main_module_name) {
    auto combined_program = std::make_unique<Program>();

    // Mapa que rastreia quais identificadores foram importados de cada módulo
    // Mapa: nome_do_módulo -> set de identificadores importados
    std::map<std::string, std::set<std::string>> imported_symbols;
    
    // Primeiro passo: coletar quais símbolos foram importados de cada módulo
    for (const auto& [mod_name, module] : modules) {
        for (const auto& import_info : module.import_infos) {
            std::string clean_dep = std::regex_replace(import_info.module_path, std::regex("\""), "");
            // Extrair nome do módulo do caminho (mesma lógica do lexer)
            size_t last_slash = clean_dep.find_last_of("/\\");
            size_t last_dot = clean_dep.find_last_of(".");
            std::string dep_name = clean_dep;
            if (last_dot != std::string::npos) {
                if (last_slash != std::string::npos) {
                    dep_name = clean_dep.substr(last_slash + 1, last_dot - last_slash - 1);
                } else {
                    dep_name = clean_dep.substr(0, last_dot);
                }
            } else if (last_slash != std::string::npos) {
                dep_name = clean_dep.substr(last_slash + 1);
            }
            
            // Adicionar identificadores importados ao conjunto
            for (const auto& [name, alias] : import_info.imports) {
                std::string symbol_name = alias.empty() ? name : alias;
                imported_symbols[dep_name].insert(symbol_name);
            }
        }
    }

    // Processar módulos em ordem topológica (dependências primeiro)
    // Usar um set para rastrear módulos já processados
    std::set<std::string> processed;
    
    // Função auxiliar para processar um módulo e suas dependências
    std::function<void(const std::string&, bool)> process_module = [&](const std::string& mod_name, bool is_main) {
        if (processed.find(mod_name) != processed.end()) return;
        
        auto it = modules.find(mod_name);
        if (it == modules.end()) return;
        
        auto& module = it->second;
        
        // Processar dependências primeiro
        for (const auto& import_info : module.import_infos) {
            std::string clean_dep = std::regex_replace(import_info.module_path, std::regex("\""), "");
            // Extrair nome do módulo do caminho (mesma lógica do lexer)
            size_t last_slash = clean_dep.find_last_of("/\\");
            size_t last_dot = clean_dep.find_last_of(".");
            std::string dep_name = clean_dep;
            if (last_dot != std::string::npos) {
                if (last_slash != std::string::npos) {
                    dep_name = clean_dep.substr(last_slash + 1, last_dot - last_slash - 1);
                } else {
                    dep_name = clean_dep.substr(0, last_dot);
                }
            } else if (last_slash != std::string::npos) {
                dep_name = clean_dep.substr(last_slash + 1);
            }
            
            // Tentar encontrar o módulo pelo nome extraído
            if (modules.find(dep_name) != modules.end()) {
                process_module(dep_name, false);  // Módulo importado, não é principal
            }
        }
        
        // Processar este módulo
        if (module.ast) {
            Program* module_program = dynamic_cast<Program*>(module.ast.get());
            const auto& imported_from_this = imported_symbols[mod_name];
            
            for (auto& stmt : module_program->get_statements()) {
                // Se for o módulo principal, incluir todos os statements
                if (is_main) {
                    combined_program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
                    continue;
                }
                
                // Para módulos importados, filtrar apenas declarações exportadas
                if (stmt->kind == NodeType::DeclarationStatement) {
                    // Incluir declarações de variáveis que foram importadas
                    auto* decl = static_cast<DeclarationStmtNode*>(stmt.get());
                    if (decl->target && decl->target->kind == NodeType::Identifier) {
                        auto* id = static_cast<IdentifierNode*>(decl->target.get());
                        if (imported_from_this.find(id->symbol) != imported_from_this.end()) {
                            combined_program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
                        }
                    }
                } else if (stmt->kind == NodeType::AssignmentExpression) {
                    // Incluir assignments que criam variáveis exportadas (quando o checker converte em declaração)
                    auto* assign = static_cast<AssignmentExprNode*>(stmt.get());
                    if (assign->target && assign->target->kind == NodeType::Identifier) {
                        auto* id = static_cast<IdentifierNode*>(assign->target.get());
                        if (imported_from_this.find(id->symbol) != imported_from_this.end()) {
                            combined_program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
                        }
                    }
                }
                // Não incluir outros tipos de statements (CallExpression, IfStatement, etc.)
            }
        }
        
        processed.insert(mod_name);
    };
    
    // Processar módulo principal primeiro (se especificado)
    if (!main_module_name.empty() && modules.find(main_module_name) != modules.end()) {
        process_module(main_module_name, true);
    }
    
    // Processar outros módulos que não foram processados (caso não tenha módulo principal especificado)
    for (auto& [name, module] : modules) {
        if (processed.find(name) == processed.end()) {
            // Se não foi especificado módulo principal, tratar todos como principais
            process_module(name, main_module_name.empty());
        }
    }

    return combined_program;
}