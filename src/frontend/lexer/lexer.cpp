#include "frontend/lexer/lexer.hpp"
#include "frontend/lexer/identifier_tokenizer.hpp"
#include "frontend/lexer/operator_tokenizer.hpp"
#include "frontend/lexer/number_tokenizer.hpp"
#include "frontend/lexer/string_tokenizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>

constexpr const char* ANSI_BOLD = "\x1b[1m";
constexpr const char* ANSI_RESET = "\x1b[0m";
constexpr const char* ANSI_RED = "\x1b[31m";

namespace {
std::string to_absolute_path(const std::string& path) {
    if (path.empty()) {
        return path;
    }
    if (path.rfind("repl[", 0) == 0 || path.rfind("notebook[", 0) == 0 || path.rfind("repl_line_", 0) == 0 || path.rfind("cell_", 0) == 0) {
        return path;
    }

    try {
        std::filesystem::path file_path(path);
        if (file_path.is_absolute()) {
            try {
                return std::filesystem::canonical(file_path).string();
            } catch (const std::filesystem::filesystem_error&) {
                return std::filesystem::absolute(file_path).string();
            }
        }

        try {
            return std::filesystem::canonical(std::filesystem::absolute(file_path)).string();
        } catch (const std::filesystem::filesystem_error&) {
            return std::filesystem::absolute(file_path).string();
        }
    } catch (const std::exception&) {
        return path;
    }
}
}

Lexer::Lexer(std::string src, std::string file)
    : input(std::move(src)), filename(std::move(file)), current(input.cbegin()), line(1), column(1), position(0)
{
    operators = {
        {"=", "="},
        {";", ";"},
        {":", ":"},
        {",", ","},
        {".", "."},
        {"(", "("},
        {")", ")"},
        {"{", "{"},
        {"}", "}"},
        {"[", "["},
        {"]", "]"},
        {"+", "+"},
        {"-", "-"},
        {"*", "*"},
        {"/", "/"},
        {"%", "%"},
        {"**", "**"},
        {"--", "--"},
        {"++", "++"},
        {"//", "//"},
        {"&&", "&&"},
        {"||", "||"},
        {"==", "=="},
        {"!=", "!="},
        {"<=", "<="},
        {">=", ">="},
        {"->", "->"},
        {"..", ".."},
        {"..=", "..="},
        {"...", "..."},
        {"+=", "+="},
        {"-=", "-="},
        {"*=", "*="},
        {"/=", "/="},
        {"//=", "//="},
        {"**=", "**="},
        {"%=", "%="},
        {"^=", "^="},
        {"<<=", "<<="},
        {">>=", ">>="},
        {"@=", "@="},
        {"@", "@"},
    };

    size_t last_slash = filename.find_last_of("/\\");
    size_t last_dot = filename.find_last_of(".");
    if (last_dot != std::string::npos) {
        module_name = filename.substr(last_slash + 1, last_dot - last_slash - 1);
    } else {
        module_name = filename.substr(last_slash + 1);
    }

    read_lines();
}

bool Lexer::is_eof() const
{
    return current == input.cend();
}

char Lexer::peek() const
{
    if (is_eof())
    {
        return '\0';
    }

    return *current;
}

void Lexer::advance()
{
    if (!is_eof())
    {
        if (*current == '\n')
        {
            line++;
            column = 1;
        }
        else
        {
            column++;
        }

        ++current;
        ++position;
    }
}

void Lexer::skip_whitespace()
{
    while (!is_eof() && std::isspace(peek()))
    {
        advance();
    }
}

bool Lexer::is_operator_start(char c)
{
    return operators.find(std::string(1, c)) != operators.end() ||
           c == '!' ||
           c == '<' ||
           c == '>' ||
           c == '.' ||
           c == '*' ||
           c == ':' ||
           c == '|' ||
           c == '&' ||
           c == '^' ||
           c == '~' ||
           c == '@' ||
           c == '%' ||
           c == '+' ||
           c == '-';
}

void Lexer::read_lines()
{
    lines.clear();
    std::istringstream stream(input);
    std::string source_line;
    while (std::getline(stream, source_line)) {
        if (!source_line.empty() && source_line.back() == '\r') {
            source_line.pop_back();
        }
        lines.push_back(source_line);
    }
    if (!input.empty() && input.back() == '\n') {
        lines.emplace_back();
    }
}

void Lexer::print_error_context(size_t error_line, size_t col_start, size_t col_end) const
{
    if (lines.empty() || error_line == 0 || error_line - 1 >= lines.size()) {
        return;
    }

    std::string line_content = lines[error_line - 1];
    std::replace(line_content.begin(), line_content.end(), '\n', ' ');

    std::cerr << " " << error_line << " |   " << line_content << "\n";

    int line_width = error_line > 0 ? static_cast<int>(std::log10(error_line) + 1) : 1;
    std::cerr << std::string(line_width, ' ') << "  |";
    std::cerr << std::string(col_start > 0 ? col_start - 1 + 3 : 3, ' ');

    std::cerr << ANSI_RED;
    size_t caret_end = std::max(col_end, col_start + 1);
    for (size_t i = col_start; i < caret_end; ++i) {
        std::cerr << "^";
    }
    std::cerr << ANSI_RESET << "\n\n";
}

void Lexer::report_error(const LexicalError& error)
{
    std::string abs_filename = to_absolute_path(error.filename().empty() ? filename : error.filename());
    diagnostics.push_back({
        abs_filename,
        error.line(),
        error.col_start(),
        std::max(error.col_end(), error.col_start() + 1),
        1,
        error.what()
    });

    if (!emit_diagnostics) {
        throw error;
    }

    std::cerr << ANSI_BOLD
              << abs_filename << ":" << error.line() << ":" << error.col_start() << ": "
              << ANSI_RED << "ERROR" << ANSI_RESET << ANSI_BOLD << ": "
              << error.what() << ANSI_RESET << "\n";
    print_error_context(error.line(), error.col_start(), error.col_end());
    std::exit(1);
}

void Lexer::set_emit_diagnostics(bool enabled)
{
    emit_diagnostics = enabled;
}

const std::vector<std::string>& Lexer::get_imported_modules() const { return imported_modules; }
const std::vector<ImportInfo>& Lexer::get_import_infos() const { return import_infos; }
const std::string& Lexer::get_module_name() const { return module_name; }

std::vector<Token> Lexer::tokenize()
{
    std::vector<Token> tokens;

    while (!is_eof())
    {
        skip_whitespace();

        if (is_eof())
            break;

        char c = peek();

        // ignore comments: `# ...` or multiline `#" ... "#`
        if (c == '#')
        {
            if (std::distance(current, input.cend()) > 1 && *(current + 1) == '"')
            {
                size_t comment_line = line;
                size_t comment_column = column;

                advance(); // #
                advance(); // "

                bool comment_closed = false;
                while (!is_eof())
                {
                    if (peek() == '"' && std::distance(current, input.cend()) > 1 && *(current + 1) == '#')
                    {
                        advance(); // "
                        advance(); // #
                        comment_closed = true;
                        break;
                    }

                    advance();
                }

                if (!comment_closed)
                {
                    report_error(LexicalError(
                        filename,
                        comment_line,
                        comment_column,
                        comment_column + 2,
                        "Unterminated multiline comment"));
                }

                continue;
            }

            advance();

            while (!is_eof() && peek() != '\n')
            {
                advance();
            }

            continue;
        }

        // strings
        if (c == '"' || c == '\'' || c == '`') {
            try {
                tokens.push_back(tokenize_string(input, position, line, column, filename));
            } catch (const LexicalError& error) {
                report_error(error);
            }
            current = input.cbegin() + position;
            continue;
        }

        // from "module" import items — detectar cedo para resolver dependências,
        // mas emitir tokens individuais para que o parser parseia normalmente.
        if (c == 'f' && std::distance(current, input.cend()) >= 4 &&
            input.substr(position, 4) == "from" &&
            (position + 4 >= input.size() ||
             !(std::isalnum(static_cast<unsigned char>(input[position + 4])) || input[position + 4] == '_')))
        {
            size_t start_pos = position;
            size_t start_col = column;
            size_t start_line = line;

            // Emitir token FROM
            for (int i = 0; i < 4; ++i) advance();
            tokens.emplace_back(TokenType::FROM, "from", start_line, start_col, column, start_pos, position, filename);
            skip_whitespace();

            if (is_eof() || peek() != '"') {
                // Não é importação de módulo (ex: `from extern ...`) — continuar lexando normalmente
                continue;
            }

            // Emitir token STRING do caminho do módulo
            Token module_token = tokenize_string(input, position, line, column, filename);
            current = input.cbegin() + position;
            std::string module_path = module_token.lexeme;
            tokens.push_back(module_token);
            skip_whitespace();

            // Verificar e emitir token IMPORT
            if (is_eof() || std::distance(current, input.cend()) < 6 ||
                input.substr(position, 6) != "import") {
                // Sem `import` após a string — não é um import statement válido
                continue;
            }
            size_t imp_start_pos = position, imp_start_col = column;
            for (int i = 0; i < 6; ++i) advance();
            tokens.emplace_back(TokenType::IMPORT, "import", start_line, imp_start_col, column, imp_start_pos, position, filename);
            skip_whitespace();

            // Coletar import_info para resolução de dependências (module manager)
            ImportInfo import_info(module_path);

            if (!is_eof() && peek() == '*') {
                // import *  [as ALIAS]
                size_t mul_pos = position, mul_col = column;
                advance();
                tokens.emplace_back(TokenType::MUL, "*", start_line, mul_col, column, mul_pos, position, filename);
                import_info.is_wildcard = true;
                skip_whitespace();

                if (!is_eof() && std::isalpha(peek())) {
                    size_t saved_pos = position, saved_col = column, saved_line = line;
                    Token maybe_as = tokenize_identifier_or_keyword(input, position, line, column, filename);
                    current = input.cbegin() + position;
                    if (maybe_as.type == TokenType::AS) {
                        tokens.push_back(maybe_as);
                        skip_whitespace();
                        Token alias_tok = tokenize_identifier_or_keyword(input, position, line, column, filename);
                        current = input.cbegin() + position;
                        if (alias_tok.type == TokenType::IDENTIFIER) {
                            import_info.wildcard_alias = alias_tok.lexeme;
                            tokens.push_back(alias_tok);
                        }
                    } else {
                        position = saved_pos; column = saved_col; line = saved_line;
                        current = input.cbegin() + position;
                    }
                }
            } else {
                // import name [as alias] [, ...]
                while (!is_eof() && peek() != ';') {
                    skip_whitespace();
                    if (peek() == ';') break;

                    Token ident_tok = tokenize_identifier_or_keyword(input, position, line, column, filename);
                    current = input.cbegin() + position;
                    if (ident_tok.type != TokenType::IDENTIFIER) break;

                    tokens.push_back(ident_tok);
                    std::string import_name = ident_tok.lexeme;
                    std::string alias;
                    size_t item_line = ident_tok.line;
                    size_t item_col_start = ident_tok.column_start;
                    size_t item_col_end   = ident_tok.column_end;
                    skip_whitespace();

                    // [as alias]
                    if (!is_eof() && std::isalpha(peek())) {
                        size_t saved_pos = position, saved_col = column, saved_line = line;
                        Token maybe_as = tokenize_identifier_or_keyword(input, position, line, column, filename);
                        current = input.cbegin() + position;
                        if (maybe_as.type == TokenType::AS) {
                            tokens.push_back(maybe_as);
                            skip_whitespace();
                            Token alias_tok = tokenize_identifier_or_keyword(input, position, line, column, filename);
                            current = input.cbegin() + position;
                            if (alias_tok.type == TokenType::IDENTIFIER) {
                                alias = alias_tok.lexeme;
                                item_line      = alias_tok.line;
                                item_col_start = alias_tok.column_start;
                                item_col_end   = alias_tok.column_end;
                                tokens.push_back(alias_tok);
                            }
                        } else {
                            position = saved_pos; column = saved_col; line = saved_line;
                            current = input.cbegin() + position;
                        }
                    }

                    import_info.imports.push_back({import_name, alias});
                    import_info.import_items.emplace_back(import_name, alias, item_line, item_col_start, item_col_end);
                    skip_whitespace();

                    if (!is_eof() && peek() == ',') {
                        size_t comma_pos = position, comma_col = column;
                        advance();
                        tokens.emplace_back(TokenType::COMMA, ",", line, comma_col, column, comma_pos, position, filename);
                        skip_whitespace();
                    } else {
                        break;
                    }
                }
            }

            skip_whitespace();
            if (!is_eof() && peek() == ';') {
                size_t semi_pos = position, semi_col = column;
                advance();
                tokens.emplace_back(TokenType::SEMICOLON, ";", line, semi_col, column, semi_pos, position, filename);
            }

            // Armazenar info de dependência (para module manager) sem emitir token extra
            import_infos.push_back(import_info);
            imported_modules.push_back(module_path);
            continue;
        }

        // identifiers or keywords
        if (std::isalpha(c) || c == '_')
        {
            tokens.push_back(tokenize_identifier_or_keyword(input, position, line, column, filename));
            current = input.cbegin() + position;
            continue;
        }

        // numbers
        if (std::isdigit(c) || (c == '-' && std::distance(current, input.cend()) > 1 && std::isdigit(*(current + 1))))
        {
            try {
                tokens.push_back(tokenize_number(input, position, line, column, filename));
            } catch (const LexicalError& error) {
                report_error(error);
            }
            current = input.cbegin() + position;
            continue;
        }

        // operators
        if (is_operator_start(c))
        {
            try {
                tokens.push_back(tokenize_operator(input, position, line, column, filename));
            } catch (const LexicalError& error) {
                report_error(error);
            }
            current = input.cbegin() + position;
            continue;
        }

        // unknown character
        size_t start_col = column;
        char ch = peek();
        report_error(LexicalError(
            filename,
            line,
            start_col,
            start_col + 1,
            "Unexpected character: '" + std::string(1, ch) + "'"));
    }

    tokens.emplace_back(TokenType::EOF_TOKEN, "EOF", line, column, column, position, position, filename);
    return tokens;
}
