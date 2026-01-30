#include "frontend/module_manager.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/checker/checker_meth.hpp"
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <regex>
#include <filesystem>

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
    module.name = lexer.get_module_name();

    if (config && ENABLE_PARSE == ENABLE_PARSE) {
        Parser parser;
        module.ast = parser.produce_ast(module.tokens);
    }
    if (config && ENABLE_CHECKING == ENABLE_CHECKING) {
        rph::Checker checker;
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
    for (const auto& dep : module.dependencies) {
        std::string clean_dep = std::regex_replace(dep, std::regex("\""), "");
        std::string dep_path = (std::filesystem::path(module.directory) / (clean_dep)).string();
        if (!std::ifstream(dep_path).good()) {
            throw std::runtime_error("Módulo " + dep + " não encontrado");
        }
        resolve_dependencies(clean_dep, dep_path, config);
    }

    visited.erase(module_name);
}

const std::map<std::string, ModuleManager::Module>& ModuleManager::get_modules() const {
    return modules;
}

void ModuleManager::compile_module(const std::string& module_name, const std::string& file_path, int config) {
    resolve_dependencies(module_name, file_path, config);
}

std::unique_ptr<Node> ModuleManager::get_combined_ast() {
    auto combined_program = std::make_unique<Program>();

    for (auto& [name, module] : modules) {
        if (module.ast) {
            Program* module_program = dynamic_cast<Program*>(module.ast.get());
            for (auto& stmt : module_program->get_statements()) {
                combined_program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
            }
        }
    }

    return combined_program;
}