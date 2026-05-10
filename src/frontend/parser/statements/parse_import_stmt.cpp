#include "frontend/parser/statements/parse_import_stmt.hpp"
#include "frontend/ast/statements/import_stmt_node.hpp"

// Parseia: from "module" import name [as alias] [, ...] ;
//          from "module" import * [as alias] ;
//
// Chamado quando current_token() == FROM e next_token() == STRING.
// O lexer já populou import_infos para resolução de dependências;
// aqui construímos o AST diretamente do token stream.
std::unique_ptr<Node> parse_import_stmt(Parser* parser) {
    // Consumir FROM
    size_t from_line = parser->current_token().line;
    size_t from_cs   = parser->current_token().column_start;
    size_t from_ps   = parser->current_token().position_start;
    parser->consume_token();

    // STRING — caminho do módulo
    if (parser->current_token().type != TokenType::STRING) {
        parser->error("Expected module path string after 'from'");
        return nullptr;
    }
    Token module_tok = parser->current_token();
    std::string module_path = module_tok.lexeme;
    std::string src_file    = module_tok.filename;
    parser->consume_token();

    // IMPORT keyword
    if (parser->current_token().type != TokenType::IMPORT) {
        parser->error("Expected 'import' after module path");
        return nullptr;
    }
    parser->consume_token();

    bool is_wildcard = false;
    std::string wildcard_alias;
    std::vector<ImportItem> items;

    if (parser->current_token().type == TokenType::MUL) {
        // import *  [as alias]
        is_wildcard = true;
        parser->consume_token();

        if (parser->current_token().type == TokenType::AS) {
            parser->consume_token();
            if (parser->current_token().type != TokenType::IDENTIFIER) {
                parser->error("Expected alias identifier after 'as'");
            } else {
                wildcard_alias = parser->current_token().lexeme;
                parser->consume_token();
            }
        }
    } else {
        // import name [as alias] [, ...]
        while (parser->not_eof() &&
               parser->current_token().type != TokenType::SEMICOLON &&
               parser->current_token().type != TokenType::EOF_TOKEN) {

            if (parser->current_token().type != TokenType::IDENTIFIER) break;

            Token name_tok = parser->current_token();
            parser->consume_token();

            std::string alias;
            size_t item_line = name_tok.line;
            size_t item_cs   = name_tok.column_start;
            size_t item_ce   = name_tok.column_end;

            if (parser->current_token().type == TokenType::AS) {
                parser->consume_token();
                if (parser->current_token().type == TokenType::IDENTIFIER) {
                    Token alias_tok = parser->current_token();
                    alias    = alias_tok.lexeme;
                    item_line = alias_tok.line;
                    item_cs   = alias_tok.column_start;
                    item_ce   = alias_tok.column_end;
                    parser->consume_token();
                }
            }

            items.emplace_back(name_tok.lexeme, alias, item_line, item_cs, item_ce);

            if (parser->current_token().type == TokenType::COMMA)
                parser->consume_token();
            else
                break;
        }
    }

    if (parser->current_token().type == TokenType::SEMICOLON)
        parser->consume_token();

    auto node = std::make_unique<ImportStmtNode>(module_path, items, src_file);
    node->is_wildcard    = is_wildcard;
    node->wildcard_alias = wildcard_alias;
    node->position = std::make_unique<PositionData>(
        module_tok.line,
        module_tok.column_start, module_tok.column_end,
        module_tok.position_start, module_tok.position_end
    );
    return node;
}
