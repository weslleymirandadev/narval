#include "frontend/parser/expressions/parse_new_expr.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"

std::unique_ptr<Node> parse_new_expr(Parser* parser) {
    // Consumir o token NEW
    parser->consume_token();
    
    // Obter o nome da classe
    auto class_name_token = parser->expect(TokenType::IDENTIFIER, "Expected class name after 'new'");
    std::string class_name = class_name_token.lexeme;
    
    auto new_expr = std::make_unique<NewExprNode>(class_name);
    
    // Parse dos argumentos do construtor
    if (parser->current_token().type == TokenType::OPAREN) {
        parser->consume_token(); // consumir (
        
        // Parse dos argumentos
        while (parser->current_token().type != TokenType::CPAREN && 
               parser->current_token().type != TokenType::EOF_TOKEN) {
            
            auto arg = parse_expr(parser);
            new_expr->arguments.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(arg.release())));
            
            // Verificar se há mais argumentos
            if (parser->current_token().type == TokenType::COMMA) {
                parser->consume_token(); // consumir ,
            } else {
                break;
            }
        }
        
        parser->expect(TokenType::CPAREN, "Expected ')' after constructor arguments");
    }
    
    return std::move(new_expr);
}
