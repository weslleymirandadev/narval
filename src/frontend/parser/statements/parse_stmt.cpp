#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/parser/statements/parse_def_stmt.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"
#include "frontend/parser/statements/parse_if_stmt.hpp"
#include "frontend/parser/statements/parse_return_stmt.hpp"
#include "frontend/parser/statements/parse_break_stmt.hpp"
#include "frontend/parser/statements/parse_continue_stmt.hpp"
#include "frontend/parser/statements/parse_for_stmt.hpp"
#include "frontend/parser/statements/parse_loop_stmt.hpp"
#include "frontend/parser/statements/parse_while_stmt.hpp"
#include "frontend/parser/statements/parse_match_stmt.hpp"

std::unique_ptr<Node> parse_stmt(Parser* parser) {
    switch (parser->current_token().type) {
        case TokenType::DEF: return parse_def_stmt(parser);
        case TokenType::IF: return parse_if_stmt(parser);
        case TokenType::RETURN: return parse_return_stmt(parser);
        case TokenType::BREAK: return parse_break_stmt(parser);
        case TokenType::CONTINUE: return parse_continue_stmt(parser);
        case TokenType::FOR: return parse_for_stmt(parser);
        case TokenType::LOOP: return parse_loop_stmt(parser);
        case TokenType::WHILE: return parse_while_stmt(parser);
        case TokenType::MATCH: return parse_match_stmt(parser);
        default: {
            auto node = parse_expr(parser);
            if (parser->current_token().type == TokenType::SEMICOLON) {
                parser->consume_token();
            }
            return node;
        }
    }
}