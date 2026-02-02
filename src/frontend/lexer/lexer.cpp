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
        {"...", "..."},
        {"+=", "+="},
        {"-=", "-="},
        {"*=", "*="},
        {"/=", "/="},
        {"//=", "//="},
        {"**=", "**="},
        {"%=", "%="},
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

        // imports - nova sintaxe: from "module" import identifier [as alias] [, ...]
        if (c == 'f' && std::distance(current, input.cend()) >= 4 && 
            input.substr(position, 4) == "from")
        {
            size_t start_pos = position;
            size_t start_col = column;
            size_t start_line = line;

            // Consome "from"
            for (int i = 0; i < 4; ++i) advance();
            skip_whitespace();
            
            if (is_eof() || peek() != '"') {
                // Não é uma importação, volta ao estado anterior e continua normalmente
                position = start_pos;
                column = start_col;
                current = input.cbegin() + position;
            } else {
                // Tokeniza a string do módulo
                Token module_token = tokenize_string(input, position, line, column, filename);
                current = input.cbegin() + position;
                
                std::string module_path = module_token.lexeme;
                
                skip_whitespace();
                
                // Verifica se há "import"
                if (is_eof() || std::distance(current, input.cend()) < 6 || 
                    input.substr(position, 6) != "import") {
                    tokens.emplace_back(TokenType::UNKNOWN, "from " + module_path, start_line, start_col, column, start_pos, position, filename);
                    continue;
                }
                
                // Consome "import"
                for (int i = 0; i < 6; ++i) advance();
                skip_whitespace();
                
                ImportInfo import_info(module_path);
                
                // Tokeniza os identificadores importados
                while (!is_eof() && peek() != ';') {
                    skip_whitespace();
                    if (peek() == ';') break;
                    
                    // Tokeniza identificador
                    Token ident_token = tokenize_identifier_or_keyword(input, position, line, column, filename);
                    current = input.cbegin() + position;
                    
                    if (ident_token.type != TokenType::IDENTIFIER) {
                        tokens.emplace_back(TokenType::UNKNOWN, "from " + module_path + " import ...", start_line, start_col, column, start_pos, position, filename);
                        break;
                    }
                    
                    std::string import_name = ident_token.lexeme;
                    std::string alias;
                    size_t item_line = ident_token.line;
                    size_t item_col_start = ident_token.column_start;
                    size_t item_col_end = ident_token.column_end;
                    
                    skip_whitespace();
                    
                    // Verifica se há "as" (precisa ser uma palavra completa)
                    if (!is_eof() && std::distance(current, input.cend()) >= 2 && 
                        input.substr(position, 2) == "as" &&
                        (position + 2 >= input.size() || !std::isalnum(input[position + 2]))) {
                        // Consome "as"
                        for (int i = 0; i < 2; ++i) advance();
                        skip_whitespace();
                        
                        // Tokeniza o alias
                        Token alias_token = tokenize_identifier_or_keyword(input, position, line, column, filename);
                        current = input.cbegin() + position;
                        
                        if (alias_token.type != TokenType::IDENTIFIER) {
                            tokens.emplace_back(TokenType::UNKNOWN, "from " + module_path + " import " + import_name + " as ...", start_line, start_col, column, start_pos, position, filename);
                            break;
                        }
                        
                        alias = alias_token.lexeme;
                        // Usar posição do alias se houver
                        item_line = alias_token.line;
                        item_col_start = alias_token.column_start;
                        item_col_end = alias_token.column_end;
                    }
                    
                    import_info.imports.push_back({import_name, alias});
                    import_info.import_items.emplace_back(import_name, alias, item_line, item_col_start, item_col_end);
                    
                    skip_whitespace();
                    
                    // Verifica se há vírgula (mais imports)
                    if (!is_eof() && peek() == ',') {
                        advance();
                        skip_whitespace();
                    } else if (!is_eof() && peek() != ';') {
                        // Erro de sintaxe
                        tokens.emplace_back(TokenType::UNKNOWN, "from " + module_path + " import ...", start_line, start_col, column, start_pos, position, filename);
                        break;
                    }
                }
                
                skip_whitespace();
                if (!is_eof() && peek() == ';') {
                    advance();
                }
                
                // Cria token de importação e armazena informações
                tokens.emplace_back(TokenType::IMPORT, "from " + module_path + " import ...", start_line, start_col, column, start_pos, position, filename);
                import_infos.push_back(import_info);
                
                // Mantém compatibilidade com código antigo
                imported_modules.push_back(module_path);
                
                continue;
            }
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