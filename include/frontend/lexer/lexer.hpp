#pragma once

#include <string>
#include <vector>
#include <cctype>
#include <stdexcept>
#include <unordered_map>
#include "frontend/lexer/token.hpp"

struct ImportItemInfo {
    std::string name;
    std::string alias;
    size_t line;
    size_t col_start;
    size_t col_end;
    
    ImportItemInfo(const std::string& n, const std::string& a, size_t l, size_t cs, size_t ce)
        : name(n), alias(a), line(l), col_start(cs), col_end(ce) {}
};

struct ImportInfo {
    std::string module_path;
    std::vector<std::pair<std::string, std::string>> imports; // (name, alias) - alias vazio se não houver (mantido para compatibilidade)
    std::vector<ImportItemInfo> import_items; // Nova estrutura com posições
    
    ImportInfo(const std::string& path) : module_path(path) {}
};

class Lexer {
    private:
        std::string input;
        std::string filename;
        std::string::const_iterator current;
        std::unordered_map<std::string, std::string> operators;
        size_t line;
        size_t column;
        size_t position;
        std::vector<std::string> imported_modules; // Mantido para compatibilidade temporária
        std::vector<ImportInfo> import_infos;      // Nova estrutura para importações detalhadas
        std::string module_name;
        
    public:
        Lexer(std::string src, std::string file);
        bool is_eof() const;
        char peek() const;
        void skip_whitespace();
        void advance();
        bool is_operator_start(char c);
        std::vector<Token> tokenize();
        const std::vector<std::string>& get_imported_modules() const;
        const std::vector<ImportInfo>& get_import_infos() const;
        const std::string& get_module_name() const;
};
