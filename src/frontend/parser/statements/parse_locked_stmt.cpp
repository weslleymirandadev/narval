#include "frontend/parser/statements/parse_locked_stmt.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"
#include "frontend/parser/expressions/parse_type.hpp"

std::unique_ptr<Node> parse_locked_stmt(Parser* parser, bool lockd) {
    auto nametoken = parser->expect(TokenType::IDENTIFIER, "Expected an identifier");
    
    size_t line = nametoken.line;
    size_t pos[2] = { nametoken.position_start, nametoken.position_end };
    size_t col[2] = { nametoken.column_start, nametoken.column_end };

    std::string namestring = nametoken.lexeme;
    auto name = std::make_unique<IdentifierNode>(namestring);
    std::string typ = "automatic";
    if (parser->current_token().type == TokenType::COLON) {
        parser->consume_token();
        typ = parse_type(parser);
    }
    
    std::unique_ptr<Node> value = nullptr;
   
    if (parser->current_token().type == TokenType::ASSIGNMENT) {
        parser->consume_token();
        value = parse_expr(parser);
    }

    parser->expect(TokenType::SEMICOLON, "Expected ';'.");

    auto node = std::make_unique<DeclarationStmtNode>(
            std::unique_ptr<Expr>(static_cast<Expr*>(name.release())),
            std::unique_ptr<Expr>(static_cast<Expr*>(value.release())),
            typ,
            lockd
        );
    if (value && value->position) {
        node->position = std::make_unique<PositionData>(value->position->line, col[0], value->position->col[1], pos[0], value->position->pos[1]);
    } else {
        node->position = std::make_unique<PositionData>(line, col[0], col[1], pos[0], pos[1]);
    }
    return node; 
}
