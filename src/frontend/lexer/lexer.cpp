#include "frontend/lexer/lexer.hpp"
#include "frontend/lexer/identifier_tokenizer.hpp"
#include "frontend/lexer/operator_tokenizer.hpp"
#include "frontend/lexer/number_tokenizer.hpp"
#include "frontend/lexer/string_tokenizer.hpp"

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
        {"...", "..."}
    };

    size_t last_slash = filename.find_last_of("/\\");
    size_t last_dot = filename.find_last_of(".");
    if (last_dot != std::string::npos) {
        module_name = filename.substr(last_slash + 1, last_dot - last_slash - 1);
    } else {
        module_name = filename.substr(last_slash + 1);
    }
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
           c == '%' ||
           c == '+' ||
           c == '-';
}

const std::vector<std::string>& Lexer::get_imported_modules() const { return imported_modules; }
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

        // ignore comments
        if (c == '#' && std::distance(current, input.cend()) > 1)
        {
            advance();
            
            while (!is_eof() && peek() != '\n')
            {
                advance();
            }
            
            continue;
        }

        // strings
        if (c == '"' || c == '\'') {
            tokens.push_back(tokenize_string(input, position, line, column, filename));
            current = input.cbegin() + position;
            continue;
        }

        // imports
        if (c == 'i' && std::distance(current, input.cend()) >= 6 && 
            input.substr(position, 6) == "import")
        {
            size_t start_pos = position;
            size_t start_col = column;
            size_t start_line = line;

            for (int i = 0; i < 6; ++i) advance();
            skip_whitespace();
            if (is_eof()) {
                tokens.emplace_back(TokenType::UNKNOWN, "import", start_line, start_col, column, start_pos, position, filename);
                continue;
            }

            std::string module_name;
            while (!is_eof() && peek() != ';' && !std::isspace(peek())) {
                module_name += peek();
                advance();
            }

            skip_whitespace();
            if (!is_eof() && peek() == ';') {
                advance();
            } else {
                tokens.emplace_back(TokenType::UNKNOWN, "import " + module_name, start_line, start_col, column, start_pos, position, filename);
                continue;
            }

            tokens.emplace_back(TokenType::IMPORT, "import " + module_name, start_line, start_col, column, start_pos, position, filename);
            imported_modules.push_back(module_name);
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
            tokens.push_back(tokenize_number(input, position, line, column, filename));
            current = input.cbegin() + position;
            continue;
        }

        // operators
        if (is_operator_start(c))
        {
            tokens.push_back(tokenize_operator(input, position, line, column, filename));
            current = input.cbegin() + position;
            continue;
        }

        // unknown character
        size_t start_col = column;
        size_t start_pos = position;
        char ch = peek();
        advance();
        tokens.emplace_back(TokenType::UNKNOWN, std::string(1, ch), line, start_col, column, start_pos, position, filename);
    }

    tokens.emplace_back(TokenType::EOF_TOKEN, "EOF", line, column, column, position, position, filename);
    return tokens;
}