#include "frontend/parser/expressions/parse_instanceof_expr.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"

std::unique_ptr<Node> parse_instanceof_expr(Parser* parser) {
    // Consumir o token INSTANCEOF
    parser->consume_token();
    
    // Parse do objeto (lado esquerdo)
    auto object = parse_expr(parser);
    
    // Obter o nome da classe
    auto class_token = parser->expect(TokenType::IDENTIFIER, "Expected class name after 'instanceof'");
    std::string class_name = class_token.lexeme;
    
    return std::make_unique<InstanceofExprNode>(
        std::unique_ptr<Expr>(static_cast<Expr*>(object.release())),
        class_name
    );
}
