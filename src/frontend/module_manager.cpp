#include "frontend/module_manager.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/checker/checker_meth.hpp"
#include "frontend/attributes/attribute_mapper.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
#include "frontend/ast/statements/import_stmt_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <regex>
#include <filesystem>
#include <functional>

std::string ModuleManager::read_file(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + file_path);
    }
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    return source;
}

std::string ModuleManager::load_module(const std::string& module_name, const std::string& file_path, int config) {
    if (modules.find(module_name) != modules.end()) return module_name;

    std::string source = read_file(file_path);
    Module module;
    module.source = source;
    module.directory = std::filesystem::path(file_path).parent_path().string();

    Lexer lexer(source, file_path);
    module.tokens = lexer.tokenize();
    module.dependencies = lexer.get_imported_modules();
    module.import_infos = lexer.get_import_infos();
    module.name = lexer.get_module_name();

    if (config & ENABLE_PARSE) {
        Parser parser;
        module.ast = parser.produce_ast(module.tokens, module.import_infos);
    }
    if (config & ENABLE_CHECKING) {
        nv::Checker checker;
        checker.apply_compilation_attributes(nv::map_compilation_attributes(module.ast.get()));
        checker.set_source_file(file_path);
        checker.check_node(module.ast.get());
    }
    const std::string loaded_name = module.name;
    modules[loaded_name] = std::move(module);
    return loaded_name;
}

namespace {
// A module is imported by name: `from "sqlite" import *;` for a bundled library,
// `from "./nested_user.nv"` for a file next to the importer. The `.nv` extension is
// optional — try the path as written, then with it.
// The language's own modules: the code is Narval and lives in stdlib/, but a program uses
// them WITHOUT importing — the compiler always pulls them in. `macros` and `sqlite` are
// deliberately not here: those are libraries you import when you want them (macros
// defines macros for your call site, sqlite loads libsqlite3 through dlopen).
const std::vector<std::string>& builtin_modules() {
    static const std::vector<std::string> names = {"strings", "grammar", "file"};
    return names;
}

// Where stdlib/ is, so a program compiles from any directory: $NARVAL_STDLIB, next to the
// executable, one level up from it (the build/ directory), or the current directory.
std::string find_stdlib_dir() {
    std::vector<std::filesystem::path> candidates;
    if (const char* env = std::getenv("NARVAL_STDLIB")) candidates.emplace_back(env);
    try {
        std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe");
        candidates.push_back(exe.parent_path() / "stdlib");
        candidates.push_back(exe.parent_path().parent_path() / "stdlib");
    } catch (...) {}
    candidates.emplace_back(std::filesystem::current_path() / "stdlib");
    for (const auto& c : candidates)
        if (std::filesystem::is_directory(c)) return c.string();
    return "";
}

std::string resolve_module_path(const std::string& dir, const std::string& requested) {
    std::filesystem::path base(dir);
    std::filesystem::path as_written = base / requested;
    if (std::ifstream(as_written).good()) return as_written.string();
    std::filesystem::path with_ext = base / (requested + ".nv");
    if (std::ifstream(with_ext).good()) return with_ext.string();
    return as_written.string();  // the caller reports it as not found
}
}  // namespace

void ModuleManager::resolve_dependencies(const std::string& module_name, const std::string& file_path, int config) {
    if (visited.find(module_name) != visited.end()) {
        throw std::runtime_error("Error: Import cycle detected with module " + module_name);
    }

    visited.insert(module_name);
    // Register by the module's own name as well, and look it up under that name:
    // keying the merge on the import path left an empty entry behind, so a
    // module's own imports were never resolved.
    const std::string loaded = load_module(module_name, file_path, config);
    visited.insert(loaded);

    auto& module = modules[loaded];
    // Usa import_infos para resolver dependências (nova sintaxe)
    for (const auto& import_info : module.import_infos) {
        std::string clean_dep = std::regex_replace(import_info.module_path, std::regex("\""), "");
        std::string dep_path = resolve_module_path(module.directory, clean_dep);
        if (!std::ifstream(dep_path).good()) {
            throw std::runtime_error("Module " + import_info.module_path + " not found");
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
            std::string dep_path = resolve_module_path(module.directory, clean_dep);
            if (!std::ifstream(dep_path).good()) {
                throw std::runtime_error("Module " + dep + " not found");
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
    // Two compilation paths cannot take the standard library along: a freestanding
    // (@[no_std]) program, which has no normal runtime, and the tensor/MLIR path, whose
    // lowering only accepts structured control flow while the library modules use `while`
    // (a pre-existing gap in that lowering, which the prelude exposed). Both markers are
    // in the source — the same way the test runner decides — so read it here instead of
    // threading a flag through every caller. When the tensor path learns to lower `while`,
    // only the no_std case is left.
    bool with_stdlib = true;
    {
        std::ifstream in(file_path);
        std::stringstream buf;
        buf << in.rdbuf();
        const std::string source = buf.str();
        with_stdlib = source.find("@[no_std]") == std::string::npos &&
                      source.find("Tensor")   == std::string::npos;
    }

    const std::string stdlib = with_stdlib ? find_stdlib_dir() : "";
    if (!stdlib.empty()) {
        for (const auto& name : builtin_modules()) {
            std::filesystem::path p = std::filesystem::path(stdlib) / (name + ".nv");
            if (std::filesystem::exists(p) && modules.find(name) == modules.end())
                load_module(name, p.string(), config);
        }
    }
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
            // IMPORTANTE: Usar o nome original (name), não o alias
            // Porque a declaração no módulo exportado usa o nome original
            if (import_info.is_wildcard) {
                // Wildcard: marcar o módulo inteiro como "incluir tudo"
                // Usamos string vazia como sentinela
                imported_symbols[dep_name].insert("*");
            } else {
                for (const auto& [name, alias] : import_info.imports) {
                    imported_symbols[dep_name].insert(name);  // Sempre usar nome original
                }
            }
        }
    }

    // A builtin module is merged as if the program had written `from "<name>" import *`.
    for (const auto& name : builtin_modules())
        if (modules.count(name)) imported_symbols[name].insert("*");

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
                // Se for o módulo principal, incluir todos os statements, exceto imports (já resolvidos)
                if (is_main) {
                    if (stmt->kind == NodeType::ImportStatement) {
                        // Manter imports wildcard com alias no combined AST para que o checker
                        // possa registrar o namespace alias no escopo principal
                        auto* import = static_cast<ImportStmtNode*>(stmt.get());
                        if (import->is_wildcard && !import->wildcard_alias.empty()) {
                            combined_program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
                        }
                    } else {
                        combined_program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
                    }
                    continue;
                }
                
                // Para módulos importados, filtrar apenas declarações exportadas
                if (stmt->kind == NodeType::DeclarationStatement) {
                    // Incluir todas as declarações de variáveis do módulo (prover contexto para funções)
                    auto* decl = static_cast<DeclarationStmtNode*>(stmt.get());
                    if (decl->target && decl->target->kind == NodeType::Identifier) {
                        combined_program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
                    }
                } else if (stmt->kind == NodeType::AssignmentExpression) {
                    // Incluir assignments que criam variáveis (quando o checker converte em declaração)
                    auto* assign = static_cast<AssignmentExprNode*>(stmt.get());
                    if (assign->target && assign->target->kind == NodeType::Identifier) {
                        combined_program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
                    }
                } else if (stmt->kind == NodeType::FunctionStatement) {
                    // Incluir todas as funções (defs) do módulo (prover contexto)
                    combined_program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
                } else if (stmt->kind == NodeType::ComptimeDecl ||
                           stmt->kind == NodeType::ComptimeFuncDef ||
                           stmt->kind == NodeType::ComptimeFor ||
                           stmt->kind == NodeType::ComptimeIf ||
                           stmt->kind == NodeType::ComptimeBlock ||
                           stmt->kind == NodeType::ComptimeWhile ||
                           stmt->kind == NodeType::ComptimeExpr ||
                           stmt->kind == NodeType::MacroCall) {
                    // Comptime state is not a symbol: `comptime` constants and
                    // macros (`sql!`, ...) are folded away by the expansion, so
                    // the imported module's copies are otherwise invisible to the
                    // importer. Carry them over so the combined program's
                    // expansion registers/expands them exactly once.
                    // ComptimeExpr/ComptimeWhile matter for the same reason and used
                    // to be dropped here, which is why `comptime import_c(...)` in an
                    // imported module declared nothing: the statement never reached
                    // the expansion, so the C prototypes were never registered.
                    combined_program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
                } else if (stmt->kind == NodeType::ClassStatement ||
                           stmt->kind == NodeType::EnumStatement ||
                           stmt->kind == NodeType::InterfaceStatement) {
                    // Incluir definições de tipos (classes, enums, interfaces) para que o checker
                    // possa resolver tipos e retornos de métodos usados no módulo principal
                    combined_program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
                }
                // Não incluir outros tipos de statements (CallExpression, IfStatement, etc.)
            }
        }
        
        processed.insert(mod_name);
    };
    
    // Processar módulo principal primeiro (se especificado)
    if (!main_module_name.empty() && modules.find(main_module_name) != modules.end()) {
        // The language's own modules are a prelude: merged BEFORE the program, so their
        // functions are checked while the program's globals do not exist yet. Merging them
        // afterwards made `count_of` in strings.nv pick up a top-level `n` declared by the
        // program (a module function body must not see the program's globals).
        for (const auto& name : builtin_modules())
            if (modules.count(name)) process_module(name, false);

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
