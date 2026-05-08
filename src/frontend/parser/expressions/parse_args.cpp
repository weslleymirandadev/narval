#include "frontend/parser/expressions/parse_args.hpp"
#include "frontend/parser/expressions/parse_arguments_list.hpp"

std::vector<std::unique_ptr<ArgNode>> parse_args(Parser* parser) {
    parser->expect(TokenType::OPAREN, "Expected '('.");

    auto args = parser->current_token().type == TokenType::CPAREN
        ? std::vector<std::unique_ptr<ArgNode>>{}
        : parse_arguments_list(parser);

    parser->expect(TokenType::CPAREN, "Expected ')'.");
    
    return args;
}