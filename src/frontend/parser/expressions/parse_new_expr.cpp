#include "frontend/parser/expressions/parse_new_expr.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"
#include "frontend/parser/expressions/parse_type.hpp"

std::unique_ptr<Node> parse_new_expr(Parser* parser) {
    Token new_tok = parser->current_token();
    parser->consume_token(); // consume 'new'

    auto class_name_token = parser->expect(TokenType::IDENTIFIER, "Expected class name after 'new'");
    std::string base_name = class_name_token.lexeme;
    std::string class_name = base_name;

    // Suporte a classes genéricas: new Box<int>(...)
    if (parser->current_token().type == TokenType::LT) {
        parser->consume_token(); // <
        std::vector<std::string> type_args;
        bool first = true;
        while (parser->not_eof() && parser->current_token().type != TokenType::GT) {
            if (!first) parser->expect(TokenType::COMMA, "Expected ',' in type arguments");
            first = false;
            type_args.push_back(parse_type(parser));
        }
        parser->expect(TokenType::GT, "Expected '>' after type arguments");
        class_name += "<";
        for (size_t i = 0; i < type_args.size(); i++) {
            if (i > 0) class_name += ", ";
            class_name += type_args[i];
        }
        class_name += ">";
    }

    auto new_expr = std::make_unique<NewExprNode>(class_name, base_name);
    new_expr->position = std::make_unique<PositionData>(
        new_tok.line,
        new_tok.column_start, new_tok.column_end,
        new_tok.position_start, new_tok.position_end,
        new_tok.filename
    );

    if (parser->current_token().type == TokenType::OPAREN) {
        parser->consume_token(); // (
        while (parser->current_token().type != TokenType::CPAREN &&
               parser->current_token().type != TokenType::EOF_TOKEN) {
            auto arg = parse_expr(parser);
            new_expr->arguments.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(arg.release())));
            if (parser->current_token().type == TokenType::COMMA)
                parser->consume_token();
            else
                break;
        }
        parser->expect(TokenType::CPAREN, "Expected ')' after constructor arguments");
    }

    return std::move(new_expr);
}
