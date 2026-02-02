#include "frontend/checker/statements/check_import_stmt.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/ast/statements/import_stmt_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/statements/def_stmt_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/checker/checker_meth.hpp"
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <vector>
#include <unordered_set>
#include <cstdint>

constexpr const char* ANSI_BOLD = "\x1b[1m";
constexpr const char* ANSI_RESET = "\x1b[0m";
constexpr const char* ANSI_RED = "\x1b[31m";
constexpr const char* ANSI_WHITE = "\x1b[37m";

namespace {
    // Converte um caminho relativo em absoluto
    std::string to_absolute_path(const std::string& path) {
        if (path.empty()) {
            return path;
        }
        
        try {
            std::filesystem::path file_path(path);
            
            // Se já é absoluto, tentar normalizar
            if (file_path.is_absolute()) {
                try {
                    return std::filesystem::canonical(file_path).string();
                } catch (const std::filesystem::filesystem_error&) {
                    return std::filesystem::absolute(file_path).string();
                }
            }
            
            // Se é relativo, converter para absoluto
            try {
                return std::filesystem::canonical(std::filesystem::absolute(file_path)).string();
            } catch (const std::filesystem::filesystem_error&) {
                return std::filesystem::absolute(file_path).string();
            }
        } catch (const std::exception&) {
            // Se falhar, retornar o caminho original
            return path;
        }
    }
}

// Conjunto estático para rastrear erros de import já reportados (evitar duplicação entre checkers)
// Usa chave composta: filename:line:col:module_path:item_name (ou apenas filename:line:col:module_path para erros gerais)
static std::unordered_set<std::string> reported_import_errors;

namespace {
    // Função auxiliar para reportar erro de import com arquivo e posição corretos
    void report_import_error(nv::Checker* ch, ImportStmtNode* import_stmt, const std::string& message, 
                            const ImportItem* specific_item = nullptr) {
        // Usar o filename do ImportStmtNode (do token original)
        std::string import_filename = import_stmt->filename;
        if (import_filename.empty()) {
            // Fallback para o filename do checker
            import_filename = ch->current_filename;
        }
        if (import_filename.empty()) {
            import_filename = ch->current_filename;
        }
        if (import_filename.empty()) {
            import_filename = "<unknown>";
        } else {
            // Converter para caminho absoluto
            import_filename = to_absolute_path(import_filename);
        }
        
        // Se temos um item específico, usar sua posição; senão usar a posição do import statement
        size_t import_line = 0;
        size_t import_col = 0;
        size_t import_col_end = 0;
        std::string item_name;
        
        if (specific_item && specific_item->line > 0) {
            import_line = specific_item->line;
            import_col = specific_item->col_start;
            import_col_end = specific_item->col_end;
            item_name = specific_item->name;
        } else if (import_stmt->position) {
            import_line = import_stmt->position->line;
            import_col = import_stmt->position->col[0];
            import_col_end = import_stmt->position->col[1];
        }
        
        // Criar chave única baseada no conteúdo para evitar duplicação entre checkers diferentes
        // Formato: filename:line:col:module_path:item_name (ou filename:line:col:module_path para erros gerais)
        std::ostringstream error_key_oss;
        error_key_oss << import_filename << ":" << import_line << ":" << import_col << ":" 
                      << import_stmt->module_path;
        if (!item_name.empty()) {
            error_key_oss << ":" << item_name;
        }
        error_key_oss << ":" << message;  // Incluir mensagem para diferenciar tipos de erro
        std::string error_key_str = error_key_oss.str();
        
        // Verificar se o erro já foi reportado globalmente (entre checkers diferentes)
        if (reported_import_errors.find(error_key_str) != reported_import_errors.end()) {
            ch->err = true;  // Manter flag de erro, mas não reportar novamente
            return;
        }
        
        // Criar chave única baseada no ponteiro para evitar duplicação dentro do mesmo checker
        const void* error_key_ptr = nullptr;
        if (specific_item) {
            // Para erros de item específico, encontrar o índice do item no vetor de imports
            size_t item_index = 0;
            for (size_t i = 0; i < import_stmt->imports.size(); ++i) {
                if (&import_stmt->imports[i] == specific_item) {
                    item_index = i;
                    break;
                }
            }
            // Criar uma chave única usando o ponteiro do import_stmt e o índice
            error_key_ptr = reinterpret_cast<const void*>(
                reinterpret_cast<uintptr_t>(import_stmt) + (item_index + 1) * sizeof(void*)
            );
        } else {
            // Para erros gerais do import, usar o ponteiro do import_stmt diretamente
            error_key_ptr = reinterpret_cast<const void*>(
                reinterpret_cast<uintptr_t>(import_stmt)
            );
        }
        
        // Verificar se o erro já foi reportado neste checker específico
        if (ch->reported_errors.find(error_key_ptr) != ch->reported_errors.end()) {
            ch->err = true;  // Manter flag de erro, mas não reportar novamente
            return;
        }
        
        // Marcar como reportado ANTES de reportar para evitar duplicação
        reported_import_errors.insert(error_key_str);
        ch->reported_errors.insert(error_key_ptr);
        
        if (import_line > 0) {
            // Ler as linhas do arquivo para mostrar contexto
            std::ifstream file(import_filename);
            std::vector<std::string> file_lines;
            if (file.is_open()) {
                std::string line;
                while (std::getline(file, line)) {
                    file_lines.push_back(line);
                }
                file.close();
            }
            
            size_t module_string_length = import_stmt->module_path.length() + 2; // +2 para as aspas

            std::cerr << ANSI_BOLD << import_filename << ":" << import_line << ":" << import_col + module_string_length << ": "
                      << ANSI_RED << "ERROR" << ANSI_RESET << ANSI_BOLD << ": "
                      << message << ANSI_RESET << "\n";
            
            // Mostrar contexto do erro (linha do import)
            if (import_line > 0 && import_line <= file_lines.size()) {
                std::string line_content = file_lines[import_line - 1];
                std::replace(line_content.begin(), line_content.end(), '\n', ' ');
                std::cerr << " " << import_line << " |   " << line_content << "\n";
                
                int line_width = import_line > 0 ? static_cast<int>(std::log10(import_line) + 1) : 1;
                std::cerr << std::string(line_width, ' ') << "  |";
                
                // Calcular espaços até a coluna inicial
                // O tamanho da string do módulo (com aspas) precisa ser considerado para alinhamento correto
                // A string aparece como "module_path" na linha, então tem module_path.length() + 2 caracteres
                
                // Para erros de identificador, as colunas já estão corretas do lexer
                // Para erros de arquivo, usar a posição da string do módulo
                // Somar o tamanho da string do módulo ao cálculo para garantir alinhamento correto
                size_t spaces_to_col = import_col - 1 + 3 + module_string_length;
                std::cerr << std::string(spaces_to_col, ' ');
                
                std::cerr << ANSI_RED;
                for (size_t i = import_col; i < import_col_end && i <= line_content.length(); ++i) {
                    std::cerr << "^";
                }
                std::cerr << ANSI_RESET << "\n\n";
            } else {
                std::cerr << "\n";
            }
        } else {
            std::cerr << ANSI_BOLD << import_filename << ": "
                      << ANSI_RED << "ERROR" << ANSI_RESET << ANSI_BOLD << ": "
                      << message << ANSI_RESET << "\n\n";
        }
        ch->err = true;
    }
    
    // Extrai símbolos exportados de um módulo (variáveis e funções)
    std::set<std::string> extract_exported_symbols(Program* program) {
        std::set<std::string> symbols;
        
        if (!program) return symbols;
        
        for (const auto& stmt : program->get_statements()) {
            // Variáveis declaradas
            if (stmt->kind == NodeType::DeclarationStatement) {
                auto* decl = static_cast<DeclarationStmtNode*>(stmt.get());
                if (decl->target && decl->target->kind == NodeType::Identifier) {
                    auto* id = static_cast<IdentifierNode*>(decl->target.get());
                    symbols.insert(id->symbol);
                }
            }
            // Funções (def)
            else if (stmt->kind == NodeType::DefStatement) {
                auto* def = static_cast<DefStmtNode*>(stmt.get());
                symbols.insert(def->name);
            }
            // Assignments que criam variáveis (serão convertidos em declarações pelo checker)
            else if (stmt->kind == NodeType::AssignmentExpression) {
                auto* assign = static_cast<AssignmentExprNode*>(stmt.get());
                if (assign->target && assign->target->kind == NodeType::Identifier) {
                    auto* id = static_cast<IdentifierNode*>(assign->target.get());
                    symbols.insert(id->symbol);
                }
            }
        }
        
        return symbols;
    }
    
    // Verifica se um identificador existe no escopo atual ou em escopos pais
    bool identifier_exists(nv::Checker* checker, const std::string& symbol) {
        try {
            checker->scope->get_key(symbol);
            return true;
        } catch (std::runtime_error&) {
            return false;
        }
    }
}

// Conjunto estático para rastrear imports já verificados (evitar erros duplicados)
static std::unordered_set<std::string> checked_imports;

std::shared_ptr<nv::Type>& check_import_stmt(nv::Checker* ch, Node* node) {
    auto* import_stmt = static_cast<ImportStmtNode*>(node);
    
    // O ModuleManager já verificou que o arquivo existe e fez parsing antes de chegar aqui
    // Então vamos apenas verificar os símbolos importados e conflitos
    // IMPORTANTE: Esta função pode ser chamada múltiplas vezes (ModuleManager + checker final),
    // então precisamos evitar reportar erros duplicados
    
    std::string module_path = import_stmt->module_path;
    
    // Criar chave única para este import (filename + linha + coluna + módulo)
    // Isso evita verificar o mesmo import múltiplas vezes quando o checker é chamado
    // tanto no ModuleManager quanto no checker final
    std::string import_key = (import_stmt->filename.empty() ? ch->current_filename : import_stmt->filename) + ":" + 
                            std::to_string(import_stmt->position ? import_stmt->position->line : 0) + ":" +
                            std::to_string(import_stmt->position ? import_stmt->position->col[0] : 0) + ":" +
                            module_path;
    
    // Se já verificamos este import, verificar se os símbolos já estão registrados
    // Se não estiverem, registrar novamente (pode acontecer se o checker foi resetado)
    bool already_checked = (checked_imports.find(import_key) != checked_imports.end());
    
    if (already_checked) {
        // Verificar se os símbolos já estão no escopo
        bool all_registered = true;
        for (const auto& item : import_stmt->imports) {
            std::string scope_name = item.alias.empty() ? item.name : item.alias;
            try {
                ch->scope->get_key(scope_name);
            } catch (std::runtime_error&) {
                all_registered = false;
                break;
            }
        }
        
        // Se todos os símbolos já estão registrados, retornar
        if (all_registered) {
            return ch->gettyptr("void");
        }
        // Caso contrário, continuar para registrar os símbolos novamente
    }
    
    // Marcar como verificado (ou re-verificado)
    checked_imports.insert(import_key);
    // Remove aspas se houver
    std::string clean_path = std::regex_replace(module_path, std::regex("\""), "");
    
    // Obter diretório do arquivo atual (mesma lógica do ModuleManager)
    std::string current_dir;
    if (!ch->current_filename.empty()) {
        current_dir = std::filesystem::path(ch->current_filename).parent_path().string();
    }
    
    // Construir caminho completo do módulo usando std::filesystem::path (suporta .., ./, etc)
    // Mesma lógica do ModuleManager: (module.directory) / (clean_dep)
    std::filesystem::path full_path_obj;
    if (std::filesystem::path(clean_path).is_absolute()) {
        full_path_obj = clean_path;
    } else {
        // Usar operator/ do filesystem::path que resolve corretamente .. e ./
        full_path_obj = std::filesystem::path(current_dir) / clean_path;
    }
    
    // Normalizar o caminho lexicamente (resolve . e .. sem verificar se o arquivo existe)
    full_path_obj = full_path_obj.lexically_normal();
    std::string full_path = full_path_obj.string();
    
    // Tentar obter caminho canônico se possível (para garantir que temos o caminho correto)
    try {
        full_path = std::filesystem::canonical(full_path_obj).string();
    } catch (const std::filesystem::filesystem_error&) {
        // Se não conseguir, usar o caminho normalizado
        // O ModuleManager já verificou que o arquivo existe, então está OK
    }
    
    // Carregar o módulo e verificar se os símbolos importados existem
    try {
        // Ler o arquivo
        std::ifstream file(full_path);
        if (!file.is_open()) {
            std::ostringstream oss;
            oss << "Failed to open file " << ANSI_BOLD << ANSI_WHITE << module_path << ANSI_RESET;
            report_import_error(ch, import_stmt, oss.str());
            return ch->gettyptr("void");
        }
        std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        
        // Fazer parsing do módulo
        Lexer lexer(source, full_path);
        auto tokens = lexer.tokenize();
        auto import_infos = lexer.get_import_infos();
        
        Parser parser;
        auto module_ast = parser.produce_ast(tokens, import_infos);
        
        if (!module_ast || module_ast->kind != NodeType::Program) {
            std::ostringstream oss;
            oss << "Failed to parse module " << ANSI_BOLD << ANSI_WHITE << module_path << ANSI_RESET;
            report_import_error(ch, import_stmt, oss.str());
            return ch->gettyptr("void");
        }
        
        auto* program = static_cast<Program*>(module_ast.get());
        auto exported_symbols = extract_exported_symbols(program);
        
        // Criar um checker temporário para verificar o módulo importado e obter os tipos
        nv::Checker module_checker;
        module_checker.set_source_file(full_path);
        module_checker.check_node(program);
        
        // Se houver erros no módulo importado, não podemos registrar os símbolos
        if (module_checker.err) {
            // Não reportar erro aqui, apenas não registrar os símbolos
            // O erro já foi reportado pelo checker do módulo
            return ch->gettyptr("void");
        }
        
        // Verificar se cada símbolo importado existe no módulo e registrar no escopo
        for (const auto& item : import_stmt->imports) {
            if (exported_symbols.find(item.name) == exported_symbols.end()) {
                std::ostringstream oss;
                oss << "Identifier '" << item.name << "' not found.";
                report_import_error(ch, import_stmt, oss.str(), &item);
                continue;  // Pular este símbolo se não existir
            }
            
            // Procurar o símbolo no AST do módulo e inferir seu tipo
            std::shared_ptr<nv::Type> symbol_type = nullptr;
            bool is_constant = false;
            bool symbol_found = false;
            
            for (const auto& stmt : program->get_statements()) {
                // Variáveis declaradas
                if (stmt->kind == NodeType::DeclarationStatement) {
                    auto* decl = static_cast<DeclarationStmtNode*>(stmt.get());
                    if (decl->target && decl->target->kind == NodeType::Identifier) {
                        auto* id = static_cast<IdentifierNode*>(decl->target.get());
                        if (id->symbol == item.name) {
                            // Inferir o tipo da declaração
                            symbol_type = module_checker.infer_expr(decl->target.get());
                            is_constant = decl->constant;
                            symbol_found = true;
                            break;
                        }
                    }
                }
                // Funções (def)
                else if (stmt->kind == NodeType::DefStatement) {
                    auto* def = static_cast<DefStmtNode*>(stmt.get());
                    if (def->name == item.name) {
                        // Construir o tipo da função diretamente do DefStmtNode
                        // Obter tipos dos parâmetros
                        std::vector<std::shared_ptr<nv::Type>> param_types;
                        for (const auto& param : def->parameters) {
                            // ParamNode tem um map parameter onde a chave é o nome e o valor é o tipo
                            // Como só precisamos do tipo, pegamos o primeiro valor do map
                            if (!param.parameter.empty()) {
                                auto type_it = param.parameter.begin();
                                std::string param_type_str = type_it->second;  // O valor é o tipo
                                auto param_type = module_checker.gettyptr(param_type_str);
                                param_types.push_back(param_type);
                            }
                        }
                        
                        // Obter tipo de retorno
                        auto return_type = module_checker.gettyptr(def->return_type);
                        
                        // Criar tipo de função
                        symbol_type = std::make_shared<nv::Def>(param_types, return_type);
                        
                        // Não generalizar aqui - a função já tem tipos concretos
                        // A generalização será feita quando necessário durante a inferência
                        
                        is_constant = true;  // Funções são constantes
                        symbol_found = true;
                        break;
                    }
                }
                // Assignments que criam variáveis (serão convertidos em declarações pelo checker)
                else if (stmt->kind == NodeType::AssignmentExpression) {
                    auto* assign = static_cast<AssignmentExprNode*>(stmt.get());
                    if (assign->target && assign->target->kind == NodeType::Identifier) {
                        auto* id = static_cast<IdentifierNode*>(assign->target.get());
                        if (id->symbol == item.name) {
                            // Inferir o tipo do assignment (será convertido em declaração)
                            symbol_type = module_checker.infer_expr(assign->target.get());
                            is_constant = false;  // Assignments criam variáveis mutáveis
                            symbol_found = true;
                            break;
                        }
                    }
                }
            }
            
            if (!symbol_type && !symbol_found) {
                // Tentar obter do escopo do módulo como fallback
                try {
                    auto& scope_type = module_checker.scope->get_key(item.name);
                    symbol_type = scope_type;
                    is_constant = (scope_type->kind == nv::Kind::DEF);
                    symbol_found = true;
                } catch (std::runtime_error&) {
                    // Símbolo não encontrado - já reportamos erro acima, apenas continuar
                    continue;
                }
            }
            
            if (!symbol_type) {
                // Não conseguimos obter o tipo do símbolo
                continue;
            }
            
            // Determinar o nome a ser usado no escopo atual:
            // - Se houver alias, usar o alias
            // - Caso contrário, usar o nome original
            std::string scope_name = item.alias.empty() ? item.name : item.alias;
            
            // Registrar o símbolo no escopo atual usando o alias (ou nome original)
            ch->scope->put_key(scope_name, symbol_type, is_constant);
        }
        
        // Verificação de conflitos removida:
        // - O ModuleManager já verifica conflitos quando combina os ASTs
        // - Verificar aqui causaria falsos positivos pois símbolos importados de outros módulos
        //   já estão no escopo quando verificamos este import
        // - Conflitos reais são detectados quando símbolos são usados, não quando importados
        
    } catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "Error processing module '" << module_path << "': " << e.what();
        report_import_error(ch, import_stmt, oss.str());
    }
    
    return ch->gettyptr("void");
}
