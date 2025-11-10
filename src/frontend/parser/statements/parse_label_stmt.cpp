#include "frontend/parser/statements/parse_label_stmt.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/parser/expressions/parse_args.hpp"
#include "frontend/parser/expressions/parse_type.hpp"

std::unique_ptr<Node> parse_label_stmt(Parser* parser) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    parser->consume_token();

    auto label_name = parser->expect(TokenType::IDENTIFIER, "Expected label identifier");

    parser->expect(TokenType::OPAREN, "Expected '(' .");

    auto label_node = std::make_unique<LabelStmtNode>(
        label_name.lexeme,
        std::vector<ParamNode>{},
        "void",
        std::vector<std::unique_ptr<Stmt>>{}
    );

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

        ParamNode param_node(param);
        param_node.position = std::move(pos_param);

        label_node->parameters.push_back(param_node);

        if (parser->current_token().type == TokenType::COMMA) {
            parser->consume_token();
        } else if (parser->current_token().type != TokenType::CPAREN) {
			parser->expect(TokenType::COMMA, "Expected ',' or ')' after parameter.");
        }
    }

    parser->expect(TokenType::CPAREN, "Expected ')'.");

    if (parser->current_token().type == TokenType::COLON) {
        parser->consume_token();
        label_node->return_type = parse_type(parser);
    }

    parser->expect(TokenType::OBRACE, "Expected '{'.");

    while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
        auto stmt = parse_stmt(parser);
        if (stmt) {
            label_node->body.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt.release())));
        }
    }

    parser->expect(TokenType::CBRACE, "Expected '}'.");

    if (label_node && label_node->position) {
        pos->col[1] = label_node->position->col[1];
        pos->pos[1] = label_node->position->pos[1];
    }

    return label_node;
}