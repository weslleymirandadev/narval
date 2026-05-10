#include "frontend/parser/statements/parse_extern_from_import_stmt.hpp"
#include "frontend/ast/statements/extern_from_import_stmt_node.hpp"

// Parse: `from extern "lang:lib" import items`
//
// Formas:
//   from extern "Python:matplotlib.pyplot" import * as plt
//   from extern "Python:numpy"             import array, zeros
//   from extern "C:math"                   import sin, cos, sqrt
//   from extern "C:stdlib"                 import *
std::unique_ptr<Node> parse_extern_from_import_stmt(Parser* parser) {
    parser->consume_token(); // consume FROM
    parser->consume_token(); // consume EXTERN

    if (parser->current_token().type != TokenType::STRING) {
        parser->error("Expected \"lang:lib\" specifier after 'from extern'");
        return nullptr;
    }

    // Separar "Python:matplotlib.pyplot" → language="Python", library="matplotlib.pyplot"
    std::string spec = parser->current_token().lexeme;
    parser->consume_token();

    std::string language, library;
    auto colon = spec.find(':');
    if (colon != std::string::npos) {
        language = spec.substr(0, colon);
        library  = spec.substr(colon + 1);
    } else {
        language = spec;
        library  = spec;
    }

    parser->expect(TokenType::IMPORT, "Expected 'import' after extern specifier");    

    bool is_wildcard = false;
    std::string wildcard_alias;
    std::vector<ExternFromImportItem> items;

    if (parser->current_token().type == TokenType::MUL) {
        // import * [as alias]
        is_wildcard = true;
        parser->consume_token(); // consume '*'
        if (parser->current_token().type == TokenType::AS) {
            parser->consume_token(); // consume 'as'
            wildcard_alias = parser->expect(TokenType::IDENTIFIER,
                                            "Expected namespace alias after 'as'").lexeme;
        }
    } else {
        // import name [as alias] [, ...]
        while (parser->not_eof() &&
               parser->current_token().type != TokenType::SEMICOLON &&
               parser->current_token().type != TokenType::EOF_TOKEN) {

            auto name = parser->expect(TokenType::IDENTIFIER, "Expected name to import").lexeme;
            std::string alias;

            if (parser->current_token().type == TokenType::AS) {
                parser->consume_token(); // consume 'as'
                alias = parser->expect(TokenType::IDENTIFIER, "Expected alias after 'as'").lexeme;
            }

            items.push_back({name, alias});

            if (parser->current_token().type == TokenType::COMMA)
                parser->consume_token();
            else
                break;
        }
    }

    if (parser->current_token().type == TokenType::SEMICOLON)
        parser->consume_token();

    return std::make_unique<ExternFromImportStmtNode>(
        language, library, std::move(items), is_wildcard, wildcard_alias);
}
