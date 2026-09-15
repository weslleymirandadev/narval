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

// Directory holding the language's own modules (stdlib/), or "" when none is found. Same
// search the compiler does: $NARVAL_STDLIB, next to the executable, one level up from it,
// the tree this compiler was built from, then the current directory.
std::string find_stdlib_dir();

// Modules a program gets WITHOUT importing them (strings, grammar, file). `macros` and
// `sqlite` are not here on purpose: those are libraries you import when you want them.
const std::vector<std::string>& builtin_modules();

class ModuleManager {
    public:
        struct Module {
            std::string name;
            std::string source;
            std::string directory;
            std::vector<Token> tokens;
            std::vector<std::string> dependencies;
            std::vector<ImportInfo> import_infos;
            std::unique_ptr<Node> ast;
        };
        
        ModuleManager() = default;
        void compile_module(const std::string& module_name, const std::string& file_path, int config);
        std::unique_ptr<Node> get_combined_ast(const std::string& main_module_name = "");
        const std::map<std::string, Module>& get_modules() const;

    private:

        // Loads a module and returns the name it registered itself under. That can
        // differ from the path it was imported by ("./mod_b.nv" vs "mod_b").
        std::string load_module(const std::string& module_name, const std::string& file_path, int config);
        void resolve_dependencies(const std::string& module_name, const std::string& file_path, int config);
        std::string read_file(const std::string& file_path);

        std::map<std::string, Module> modules;
        std::set<std::string> visited;
};