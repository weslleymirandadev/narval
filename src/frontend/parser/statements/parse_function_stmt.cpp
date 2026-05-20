#include "frontend/parser/statements/parse_function_stmt.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/parser/statements/parse_block_util.hpp"
#include "frontend/parser/expressions/parse_args.hpp"
#include "frontend/parser/expressions/parse_type.hpp"
#include "frontend/parser/expressions/parse_assignment_expr.hpp"

std::unique_ptr<Node> parse_function_stmt(Parser* parser) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    parser->consume_token();

    auto function_name = parser->expect(TokenType::IDENTIFIER, "Expected function name");
    auto function_node = std::make_unique<FunctionStmtNode>(
        function_name.lexeme,
        std::vector<ParamNode>{},
        "None",
        std::vector<std::unique_ptr<Stmt>>{}
    );


    if (parser->current_token().type == TokenType::COLON) {
        parser->consume_token();
        function_node->return_type = parse_type(parser);

        parser->expect(TokenType::OBRACE, "Expected '{'.");
        function_node->body = parse_body(parser);
        parser->expect(TokenType::CBRACE, "Expected '}'.");

        if (function_node && function_node->position) {
            pos->col[1] = function_node->position->col[1];
            pos->pos[1] = function_node->position->pos[1];
        }

        return function_node;
    }

    parser->expect(TokenType::OPAREN, "Expected '(' .");

    while (parser->not_eof() && parser->current_token().type != TokenType::CPAREN) {
        size_t line_param = parser->current_token().line;
        size_t column_param[2] = { parser->current_token().column_start, parser->current_token().column_end };
        size_t position_param[2] = { parser->current_token().position_start, parser->current_token().position_end };
        std::unique_ptr<PositionData> pos_param = std::make_unique<PositionData>(line_param, column_param[0], column_param[1], position_param[0], position_param[1]);

        auto arg_name_token = parser->expect(TokenType::IDENTIFIER, "Expected argument name.");
        parser->expect(TokenType::COLON, "Expected ':'.");
        std::string arg_type = parse_type(parser);

        std::unordered_map<std::string, std::string> param;
        param[arg_name_token.lexeme] = arg_type;

        // Check for default value: name: type = expression
        std::unique_ptr<Expr> default_val = nullptr;
        if (parser->current_token().type == TokenType::ASSIGNMENT) {
            parser->consume_token(); // consume '='
            // Parse default value expression
            default_val = std::unique_ptr<Expr>(static_cast<Expr*>(parse_assignment_expr(parser).release()));
        }

        ParamNode param_node(param, std::move(default_val));
        param_node.position = std::move(pos_param);

        function_node->parameters.push_back(param_node);

        if (parser->current_token().type == TokenType::COMMA) {
            parser->consume_token();
        } else if (parser->current_token().type != TokenType::CPAREN) {
			parser->expect(TokenType::COMMA, "Expected ',' or ')' after parameter.");
        }
    }

    parser->expect(TokenType::CPAREN, "Expected ')'.");

    if (parser->current_token().type == TokenType::COLON) {
        parser->consume_token();
        function_node->return_type = parse_type(parser);
    }

    parser->expect(TokenType::OBRACE, "Expected '{'.");
    function_node->body = parse_body(parser);
    parser->expect(TokenType::CBRACE, "Expected '}'.");

    if (function_node && function_node->position) {
        pos->col[1] = function_node->position->col[1];
        pos->pos[1] = function_node->position->pos[1];
    }

    return function_node;
}