#pragma once

#include <string>
#include <vector>
#include <cctype>
#include <stdexcept>
#include <unordered_map>
#include "frontend/lexer/token.hpp"

class Lexer {
    private:
        std::string input;
        std::string filename;
        std::string::const_iterator current;
        std::unordered_map<std::string, std::string> operators;
        size_t line;
        size_t column;
        size_t position;
        std::vector<std::string> imported_modules;
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
        const std::string& get_module_name() const;
};
