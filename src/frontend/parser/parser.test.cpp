#include <iostream>
#include <fstream>
#include <string>
#include "frontend/lexer/lexer.hpp"
#include "frontend/lexer/token.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/module_manager.hpp"

int main(int argc, char* argv[]) {
    std::string filename = "../test/main.phi";
    std::string module_name = "main";

    ModuleManager module_manager;
    try {
        std::cout << "Iniciando teste de análise sintática...\n";
        module_manager.compile_module(module_name, filename, true);

        std::cout << "\nASTs geradas para cada módulo:\n";
        const auto& modules = module_manager.get_modules();
        for (const auto& [name, module] : modules) {
            std::cout << "\nMódulo: " << name << "\n";
            std::cout << "Dependências: ";
            if (module.dependencies.empty()) {
                std::cout << "Nenhuma\n";
            } else {
                for (const auto& dep : module.dependencies) {
                    size_t lastSlash = dep.find_last_of("/\\");
                    std::string fileName = (lastSlash == std::string::npos) ? dep : dep.substr(lastSlash + 1);
                    size_t lastDot = fileName.find_last_of(".");
                    std::cout << ((lastDot == std::string::npos) ? fileName : fileName.substr(0, lastDot)) << " ";
                }
                std::cout << "\n";
            }
            std::cout << "AST:\n";
            if (module.ast) {
                dynamic_cast<Program*>(module.ast.get())->print();
            } else {
                std::cout << "  Nenhuma AST gerada.\n";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Erro durante teste de análise sintática: " << e.what() << "\n";
        return 1;
    }

    std::cout << "\nTeste de análise sintática concluído com sucesso.\n";
    return 0;
}
