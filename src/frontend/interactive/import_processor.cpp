#include "frontend/interactive/import_processor.hpp"
#include <sstream>
#include <iostream>
#include <filesystem>
#include "frontend/checker/type.hpp"

namespace nv {

bool ImportProcessor::process_imports(
    const std::string& input,
    std::unique_ptr<Node>& ast,
    REPLState* state,
    ModuleManager& module_manager
) {
    if (!has_imports(input)) {
        return true;
    }
    
    auto imports_to_process = extract_imports(input);
    
    // Coletar todos os statements de importação
    std::vector<std::unique_ptr<Stmt>> import_statements;
    
    // Processa cada importação usando ModuleManager
    for (const auto& [module_path, imported_names] : imports_to_process) {
        if (!process_single_import(module_path, imported_names, import_statements, state, module_manager)) {
            return false;
        }
    }
    
    // Criar novo programa com importações primeiro
    auto new_program = std::make_unique<Program>();
    
    // PRIMEIRO: Adicionar todas as importações
    for (auto& stmt : import_statements) {
        new_program->add_statement(std::move(stmt));
    }
    
    // DEPOIS: Adicionar os statements originais (exceto imports)
    if (auto* prog = dynamic_cast<Program*>(ast.get())) {
        for (const auto& stmt : prog->get_statements()) {
            // Pular imports (já processados)
            if (stmt->kind != NodeType::ImportStatement) {
                new_program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
            }
        }
    }
    
    // Substitui o AST original pelo novo (com importações primeiro)
    ast = std::move(new_program);
    
    return true;
}

bool ImportProcessor::has_imports(const std::string& input) {
    std::stringstream input_lines(input);
    std::string line;
    while (std::getline(input_lines, line)) {
        if (line.find("from") != std::string::npos && 
            line.find("import") != std::string::npos && 
            line.find(";") != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::map<std::string, std::vector<std::string>> ImportProcessor::extract_imports(const std::string& input) {
    std::map<std::string, std::vector<std::string>> imports_to_process;
    
    // Extrair a primeira linha que contém import
    std::string import_line;
    std::stringstream input_lines(input);
    std::string line;
    while (std::getline(input_lines, line)) {
        if (line.find("from") != std::string::npos && 
            line.find("import") != std::string::npos && 
            line.find(";") != std::string::npos) {
            import_line = line;
            break;
        }
    }
    
    // Parsing: from "path" import name1, name2;
    size_t from_pos = import_line.find("from");
    size_t import_pos = import_line.find("import");
    
    if (from_pos != std::string::npos && import_pos != std::string::npos) {
        // Extrair module path (entre aspas)
        size_t path_start = import_line.find("\"", from_pos);
        size_t path_end = import_line.find("\"", path_start + 1);
        if (path_start != std::string::npos && path_end != std::string::npos) {
            std::string module_path = import_line.substr(path_start + 1, path_end - path_start - 1);
            
            // Extrair nomes importados (após "import" até o ; obrigatório)
            size_t names_start = import_pos + 6; // após "import"
            size_t names_end = import_line.find(";", names_start);
            if (names_end == std::string::npos) {
                // Se não encontrar ;, não é um import válido
                return {};
            }
            
            std::string names_str = import_line.substr(names_start, names_end - names_start);
            std::vector<std::string> imported_names;
            std::stringstream ss(names_str);
            std::string name;
            while (std::getline(ss, name, ',')) {
                // Remover espaços em branco
                name.erase(0, name.find_first_not_of(" \t"));
                name.erase(name.find_last_not_of(" \t") + 1);
                if (!name.empty()) {
                    imported_names.push_back(name);
                }
            }
            
            imports_to_process[module_path] = imported_names;
        }
    }
    
    return imports_to_process;
}

bool ImportProcessor::process_single_import(
    const std::string& module_path,
    const std::vector<std::string>& imported_names,
    std::vector<std::unique_ptr<Stmt>>& import_statements,
    REPLState* state,
    ModuleManager& module_manager
) {
    try {
        // Compila o módulo importado
        std::string clean_path = module_path;
        if (clean_path.size() >= 2 && clean_path.front() == '"' && clean_path.back() == '"') {
            clean_path = clean_path.substr(1, clean_path.size() - 2);
        }
        
        std::string module_name = std::filesystem::path(clean_path).stem().string();
        module_manager.compile_module(module_name, clean_path, true);
        
        // Obtém o módulo compilado
        const auto& modules = module_manager.get_modules();
        auto it = modules.find(module_name);
        if (it != modules.end()) {
            const auto& module = it->second;
            if (auto* module_program = dynamic_cast<Program*>(module.ast.get())) {
                // Adiciona todos os símbolos do módulo em ordem (variáveis primeiro, depois funções)
                std::vector<const Stmt*> variable_stmts;
                std::vector<const Stmt*> function_stmts;
                std::set<std::string> imported_set(imported_names.begin(), imported_names.end());
                std::set<std::string> required_variables;
                std::set<std::string> required_functions;
                
                // Primeira passagem: identifica as funções importadas e suas dependências
                for (const auto& stmt : module_program->get_statements()) {
                    if (stmt->kind == NodeType::DefStatement) {
                        auto* def_stmt = static_cast<const DefStmtNode*>(stmt.get());
                        if (imported_set.find(def_stmt->name) != imported_set.end()) {
                            function_stmts.push_back(stmt.get());
                            
                            // Analisa o corpo da função para encontrar identifiers usados
                            for (const auto& body_stmt : def_stmt->body) {
                                auto ids = extract_identifiers(body_stmt.get());
                                for (const auto& id : ids) {
                                    // Ignora parâmetros da função
                                    bool is_param = false;
                                    for (const auto& param : def_stmt->parameters) {
                                        for (const auto& [param_name, param_type] : param.parameter) {
                                            if (param_name == id) {
                                                is_param = true;
                                                break;
                                            }
                                        }
                                    }
                                    if (!is_param) {
                                        // Verifica se é uma variável ou função disponível no módulo
                                        for (const auto& check_stmt : module_program->get_statements()) {
                                            if (check_stmt->kind == NodeType::DeclarationStatement) {
                                                auto* check_decl = static_cast<const DeclarationStmtNode*>(check_stmt.get());
                                                if (check_decl->target && check_decl->target->kind == NodeType::Identifier) {
                                                    auto* check_id = static_cast<const IdentifierNode*>(check_decl->target.get());
                                                    if (check_id->symbol == id) {
                                                        required_variables.insert(id);
                                                        break;
                                                    }
                                                }
                                            } else if (check_stmt->kind == NodeType::DefStatement) {
                                                auto* check_def = static_cast<const DefStmtNode*>(check_stmt.get());
                                                if (check_def->name == id) {
                                                    required_functions.insert(id);
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                
                // Segunda passagem: adiciona variáveis necessárias
                for (const auto& stmt : module_program->get_statements()) {
                    if (stmt->kind == NodeType::DeclarationStatement) {
                        auto* decl_stmt = static_cast<const DeclarationStmtNode*>(stmt.get());
                        if (decl_stmt->target && decl_stmt->target->kind == NodeType::Identifier) {
                            auto* id = static_cast<const IdentifierNode*>(decl_stmt->target.get());
                            // Adiciona variável se for requerida por alguma função importada OU se foi originalmente importada
                            if (required_variables.find(id->symbol) != required_variables.end() || 
                                imported_set.find(id->symbol) != imported_set.end()) {
                                variable_stmts.push_back(stmt.get());
                            }
                        }
                    } else if (stmt->kind == NodeType::DefStatement) {
                        auto* def_stmt = static_cast<const DefStmtNode*>(stmt.get());
                        // Adiciona função se for requerida por alguma função importada OU se foi originalmente importada
                        if (required_functions.find(def_stmt->name) != required_functions.end() || 
                            imported_set.find(def_stmt->name) != imported_set.end()) {
                            function_stmts.push_back(stmt.get());
                        }
                    }
                }
                
                // Adiciona variáveis primeiro
                for (const auto* stmt : variable_stmts) {
                    if (stmt->kind == NodeType::DeclarationStatement) {
                        auto* decl_stmt = static_cast<const DeclarationStmtNode*>(stmt);
                        if (decl_stmt->target && decl_stmt->target->kind == NodeType::Identifier) {
                            auto* id = static_cast<const IdentifierNode*>(decl_stmt->target.get());
                            state->repl_global_names.insert(id->symbol);
                            
                            // Adiciona ao scope do checker também!
                            auto var_type = state->checker->infer_type(decl_stmt->value.get());
                            state->checker->scope->put_key(id->symbol, var_type, false);
                            
                            // Adiciona ao vetor de importações
                            import_statements.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
                        }
                    }
                }
                
                // Adiciona funções depois
                for (const auto* stmt : function_stmts) {
                    if (stmt->kind == NodeType::DefStatement) {
                        auto* def_stmt = static_cast<const DefStmtNode*>(stmt);
                        state->repl_global_names.insert(def_stmt->name);
                        
                        // Adiciona ao scope do checker também!
                        // Para funções, criamos um tipo Def básico
                        std::vector<std::shared_ptr<Type>> param_types;
                        for (const auto& param : def_stmt->parameters) {
                            for (const auto& [param_name, param_type] : param.parameter) {
                                // Criar tipo básico para o parâmetro
                                if (param_type == "int") {
                                    param_types.push_back(std::make_shared<Int>());
                                } else if (param_type == "string") {
                                    param_types.push_back(std::make_shared<String>());
                                } else if (param_type == "float") {
                                    param_types.push_back(std::make_shared<Float>());
                                } else if (param_type == "bool") {
                                    param_types.push_back(std::make_shared<Boolean>());
                                } else {
                                    param_types.push_back(std::make_shared<TypeVar>(0)); // Tipo variável como fallback
                                }
                            }
                        }
                        
                        // Tipo de retorno
                        std::shared_ptr<Type> return_type;
                        if (def_stmt->return_type == "int") {
                            return_type = std::make_shared<Int>();
                        } else if (def_stmt->return_type == "string") {
                            return_type = std::make_shared<String>();
                        } else if (def_stmt->return_type == "float") {
                            return_type = std::make_shared<Float>();
                        } else if (def_stmt->return_type == "bool") {
                            return_type = std::make_shared<Boolean>();
                        } else {
                            return_type = std::make_shared<Void>();
                        }
                        
                        auto func_type = std::make_shared<Def>(param_types, return_type);
                        
                        // Adiciona ao scope
                        state->checker->scope->put_key(def_stmt->name, func_type, false);
                        
                        // Adiciona ao vetor de importações
                        import_statements.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
                    }
                }
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error processing import " << module_path << ": " << e.what() << std::endl;
        return false;
    }
}

std::set<std::string> ImportProcessor::extract_identifiers(const Node* node) {
    std::set<std::string> ids;
    
    if (!node) return ids;
    
    switch (node->kind) {
        case NodeType::CallExpression: {
            auto* call_expr = static_cast<const CallExprNode*>(node);
            if (call_expr->caller && call_expr->caller->kind == NodeType::Identifier) {
                auto* id = static_cast<const IdentifierNode*>(call_expr->caller.get());
                ids.insert(id->symbol);
            }
            // Analisa os argumentos
            for (const auto& arg : call_expr->args) {
                auto arg_ids = extract_identifiers(arg.get());
                ids.insert(arg_ids.begin(), arg_ids.end());
            }
            break;
        }
        case NodeType::Identifier: {
            auto* id = static_cast<const IdentifierNode*>(node);
            ids.insert(id->symbol);
            break;
        }
        case NodeType::BinaryExpression: {
            auto* bin_expr = static_cast<const BinaryExprNode*>(node);
            auto left_ids = extract_identifiers(bin_expr->left.get());
            auto right_ids = extract_identifiers(bin_expr->right.get());
            ids.insert(left_ids.begin(), left_ids.end());
            ids.insert(right_ids.begin(), right_ids.end());
            break;
        }
        case NodeType::ReturnStatement: {
            auto* return_stmt = static_cast<const ReturnStmtNode*>(node);
            if (return_stmt->value) {
                auto value_ids = extract_identifiers(return_stmt->value.get());
                ids.insert(value_ids.begin(), value_ids.end());
            }
            break;
        }
        default:
            break;
    }
    
    return ids;
}

} // namespace nv
