#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/checker/checker_meth.hpp"
#define ENABLE_PARSE 1
#define ENABLE_CHECKING 2
#define ENABLE_GENERATION 4

class ModuleManager {
    public:
        struct Module {
            std::string name;
            std::string source;
            std::string directory;
            std::vector<Token> tokens;
            std::vector<std::string> dependencies;
            std::unique_ptr<Node> ast;
        };
        
        ModuleManager() = default;
        void compile_module(const std::string& module_name, const std::string& file_path, int config);
        std::unique_ptr<Node> get_combined_ast();
        const std::map<std::string, Module>& get_modules() const;

    private:

        void load_module(const std::string& module_name, const std::string& file_path, int config);
        void resolve_dependencies(const std::string& module_name, const std::string& file_path, int config);
        std::string read_file(const std::string& file_path);

        std::map<std::string, Module> modules;
        std::set<std::string> visited;
};