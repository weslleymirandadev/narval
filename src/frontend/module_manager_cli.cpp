#include "frontend/module_manager.hpp"
#include "frontend/ast/program.hpp"
#include "frontend/checker/checker.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void print_usage(const char* program_name) {
    std::cout << "Uso: " << program_name << " [opcoes] arquivo.nv\n"
              << "\nOpcoes:\n"
              << "  --tokens       Lista os tokens de cada modulo carregado\n"
              << "  --ast          Imprime a AST combinada apos resolver imports\n"
              << "  --help, -h     Mostra esta ajuda\n";
}

std::string module_name_from_path(const std::string& filename) {
    return std::filesystem::path(filename).stem().string();
}

} // namespace

int main(int argc, char* argv[]) {
    bool print_tokens = false;
    bool print_ast = false;
    std::string filename;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--tokens") {
            print_tokens = true;
        } else if (arg == "--ast") {
            print_ast = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Opcao desconhecida: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        } else {
            filename = arg;
        }
    }

    if (filename.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        const std::string module_name = module_name_from_path(filename);

        ModuleManager module_manager;
        module_manager.compile_module(module_name, filename, ENABLE_PARSE);

        if (print_tokens) {
            for (const auto& [name, module] : module_manager.get_modules()) {
                std::cout << "Modulo: " << name << "\n";
                for (const auto& token : module.tokens) {
                    std::cout << "  " << get_token_name(token.type)
                              << " '" << token.lexeme << "'"
                              << " " << token.line << ":"
                              << token.column_start << "-"
                              << token.column_end << "\n";
                }
            }
        }

        auto ast = module_manager.get_combined_ast(module_name);
        if (!ast) {
            throw std::runtime_error("AST combinada nao foi gerada");
        }

        if (print_ast) {
            if (auto* program = dynamic_cast<Program*>(ast.get())) {
                program->print();
            } else {
                std::cout << "AST combinada nao e um Program\n";
            }
        }

        nv::Checker checker;
        checker.set_source_file(filename);
        checker.check_node(ast.get());

        if (checker.err) {
            return 1;
        }

        std::cout << "OK: analise lexica, sintatica e semantica concluida\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Erro: " << e.what() << "\n";
        return 1;
    }
}
