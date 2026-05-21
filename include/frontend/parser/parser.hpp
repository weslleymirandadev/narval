#pragma once
#include <vector>
#include <string>
#include <memory>
#include "frontend/diagnostic.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/lexer/token.hpp"
#include "frontend/lexer/lexer.hpp"

class Parser {
    private:
        std::vector<Token> tokens;
        size_t token_count = 0;
        bool has_errors = false;
        std::vector<std::string> lines;
        size_t line_count = 0;
        size_t index = 0;
        std::vector<ImportInfo> import_infos;
        size_t import_index = 0;
        bool emit_diagnostics = true;

        void read_lines(const std::string& filename);
        void print_error_context(const Token& token);

    public:
        Parser() = default;

        bool has_error() const;
        void set_emit_diagnostics(bool enabled);
        void error(const std::string& message);
        size_t get_token_count() const;
        std::vector<nv::Diagnostic> diagnostics;

        bool not_eof() const;
        Token current_token() const;
        Token consume_token();
        Token next_token() const;
        // Lookahead sem consumir: offset=0 → current, 1 → next, etc.
        Token peek_at(size_t offset) const;
        Token expect(TokenType expectedType, const std::string& errorMsg);


        std::unique_ptr<Node> produce_ast(const std::vector<Token>& tokens, const std::vector<ImportInfo>& imports = {});
};
