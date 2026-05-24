#include "frontend/parser/statements/parse_decorator_stmt.hpp"
#include "frontend/ast/statements/decorator_stmt_node.hpp"

// Parse:
//   @name               – simple decorator
//   @name(arg, k=v, ...)– decorator factory (function that returns a decorator)
//
// Examples:
//   @memoize
//   @log
//   @optimize(tile=[16,16,4], vectorize)
//   @retry(max=3)
std::unique_ptr<Node> parse_decorator_stmt(Parser* parser) {
    Token at_tok = parser->consume_token();  // '@'

    auto node = std::make_unique<DecoratorStmtNode>();

    if (parser->current_token().type == TokenType::OBRACKET) {
        parser->error("Use '@[name]' for compiler attributes; '@name' or '@name(args)' for decorators");
        return nullptr;
    }

    if (parser->current_token().type != TokenType::IDENTIFIER) {
        parser->error("Expected identifier after '@' for decorator");
        return nullptr;
    }

    Token name_tok = parser->consume_token();

    DecoratorEntry entry;
    entry.name = name_tok.lexeme;

    // Optional argument list: @name(arg, key=val, ...)
    if (parser->current_token().type == TokenType::OPAREN) {
        parser->consume_token();  // '('

        while (parser->current_token().type != TokenType::CPAREN &&
               parser->current_token().type != TokenType::EOF_TOKEN) {
            DecoratorArg arg;

            // key=value pair
            if (parser->current_token().type == TokenType::IDENTIFIER &&
                parser->peek_at(1).type      == TokenType::ASSIGNMENT) {
                arg.key = parser->consume_token().lexeme;  // key
                parser->consume_token();                    // '='
            }

            // Value: identifier/keyword, number, string, or bracketed list
            Token val = parser->current_token();
            if (val.type == TokenType::IDENTIFIER ||
                val.type == TokenType::NUMBER      ||
                val.type == TokenType::STRING      ||
                val.type == TokenType::TRUE        ||
                val.type == TokenType::FALSE) {
                arg.value = val.lexeme;
                parser->consume_token();
            } else if (val.type == TokenType::OBRACKET) {
                // [M, N, K] → store as "M,N,K"
                parser->consume_token();  // '['
                std::string list;
                while (parser->current_token().type != TokenType::CBRACKET &&
                       parser->current_token().type != TokenType::EOF_TOKEN) {
                    if (parser->current_token().type != TokenType::COMMA)
                        list += parser->current_token().lexeme;
                    else
                        list += ",";
                    parser->consume_token();
                }
                if (parser->current_token().type == TokenType::CBRACKET)
                    parser->consume_token();  // ']'
                arg.value = list;
            }

            entry.args.push_back(arg);

            if (parser->current_token().type == TokenType::COMMA)
                parser->consume_token();
        }

        if (parser->current_token().type == TokenType::CPAREN)
            parser->consume_token();  // ')'
    }

    node->entries.push_back(entry);
    node->decorators.push_back(name_tok.lexeme);  // legacy compat

    node->position = std::make_unique<PositionData>(
        at_tok.line, at_tok.column_start, name_tok.column_end,
        at_tok.position_start, name_tok.position_end, at_tok.filename);

    return node;
}
