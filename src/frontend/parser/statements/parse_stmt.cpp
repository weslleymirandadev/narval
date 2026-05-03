#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/parser/statements/parse_def_stmt.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"
#include "frontend/parser/statements/parse_if_stmt.hpp"
#include "frontend/parser/statements/parse_return_stmt.hpp"
#include "frontend/parser/statements/parse_break_stmt.hpp"
#include "frontend/parser/statements/parse_continue_stmt.hpp"
#include "frontend/parser/statements/parse_for_stmt.hpp"
#include "frontend/parser/statements/parse_forever_stmt.hpp"
#include "frontend/parser/statements/parse_while_stmt.hpp"
#include "frontend/parser/statements/parse_match_stmt.hpp"
#include "frontend/parser/statements/parse_declaration_stmt.hpp"
#include "frontend/parser/statements/parse_class_def.hpp"
#include "frontend/parser/statements/parse_interface_def.hpp"
#include "frontend/parser/statements/parse_try_stmt.hpp"
#include "frontend/parser/statements/parse_throw_stmt.hpp"
#include "frontend/parser/statements/parse_enum_def.hpp"

std::unique_ptr<Node> parse_stmt(Parser* parser) {
    switch (parser->current_token().type) {
        case TokenType::MUT: 
            parser->consume_token(); // Consumir o token MUT
            return parse_declaration_stmt(parser, true);
        case TokenType::CLASS: return parse_class_def(parser);
        case TokenType::INTERFACE: return parse_interface_def(parser);
        case TokenType::ENUM: return parse_enum_def(parser);
        case TokenType::DEF: return parse_def_stmt(parser);
        case TokenType::IF: return parse_if_stmt(parser);
        case TokenType::RETURN: return parse_return_stmt(parser);
        case TokenType::BREAK: return parse_break_stmt(parser);
        case TokenType::CONTINUE: return parse_continue_stmt(parser);
        case TokenType::FOR: return parse_for_stmt(parser);
        case TokenType::FOREVER: return parse_forever_stmt(parser);
        case TokenType::WHILE: return parse_while_stmt(parser);
        case TokenType::MATCH: return parse_match_stmt(parser);
        case TokenType::TRY: return parse_try_stmt(parser);
        case TokenType::THROW: return parse_throw_stmt(parser);
        case TokenType::PROPAGATE: {
            auto node = std::make_unique<PropagateStmtNode>();
            parser->consume_token();
            if (parser->current_token().type == TokenType::SEMICOLON)
                parser->consume_token();
            return node;
        }
        default: {
            auto node = parse_expr(parser);
            if (parser->current_token().type == TokenType::SEMICOLON) {
                parser->consume_token();
            }
            return node;
        }
    }
}