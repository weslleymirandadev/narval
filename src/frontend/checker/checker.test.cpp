#include <iostream>
#include <fstream>
#include <string>
#include "frontend/lexer/lexer.hpp"
#include "frontend/lexer/token.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/module_manager.hpp"

int main(int argc, char* argv[]) {
    std::string filename = "../test/main.phi";
    std::string module_name = "main";

    ModuleManager module_manager;
    try {
        std::cout << "Iniciando teste de análise semântica...\n";
        module_manager.compile_module(module_name, filename, ENABLE_PARSE & ENABLE_CHECKING);

        std::cout << "\nStatus de cada módulo cada módulo:\n";
        const auto& modules = module_manager.get_modules();
        
        
    } catch (const std::exception& e) {
        std::cerr << "Erro durante teste de análise semântica: " << e.what() << "\n";
        return 1;
    }

    std::cout << "\nTeste de análise semântica concluído com sucesso.\n";
    return 0;
}
