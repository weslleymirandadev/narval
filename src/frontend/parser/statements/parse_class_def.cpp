#include "frontend/parser/statements/parse_class_def.hpp"
#include "frontend/parser/statements/parse_def_stmt.hpp"
#include "frontend/parser/expressions/parse_type.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"

std::unique_ptr<Node> parse_class_def(Parser* parser) {
    // Consumir o token CLASS
    parser->consume_token();
    
    // Obter o nome da classe
    auto name_token = parser->expect(TokenType::IDENTIFIER, "Expected class name");
    std::string class_name = name_token.lexeme;
    
    auto class_node = std::make_unique<ClassDefNode>(class_name);
    
    // Verificar se há herança (extends)
    if (parser->current_token().type == TokenType::EXTENDS) {
        parser->consume_token(); // consumir EXTENDS
        auto parent_token = parser->expect(TokenType::IDENTIFIER, "Expected parent class name after 'extends'");
        class_node->parent_class = parent_token.lexeme;
    }
    
    // Esperar {
    parser->expect(TokenType::OBRACE, "Expected '{' after class name");
    
    // Parse do corpo da classe
    while (parser->current_token().type != TokenType::CBRACE && 
           parser->current_token().type != TokenType::EOF_TOKEN) {
        
        // Verificar se é um modificador de acesso
        std::string access_modifier = "public"; // padrão
        if (parser->current_token().type == TokenType::PUBLIC ||
            parser->current_token().type == TokenType::PRIVATE ||
            parser->current_token().type == TokenType::PROTECTED) {
            
            if (parser->current_token().type == TokenType::PUBLIC) {
                access_modifier = "public";
            } else if (parser->current_token().type == TokenType::PRIVATE) {
                access_modifier = "private";
            } else if (parser->current_token().type == TokenType::PROTECTED) {
                access_modifier = "protected";
            }
            parser->consume_token();
        }
        
        // Verificar se é um campo ou método
        if (parser->current_token().type == TokenType::IDENTIFIER) {
            std::string member_name = parser->current_token().lexeme;
            parser->consume_token();
            
            // Se o próximo token for :, é um campo
            if (parser->current_token().type == TokenType::COLON) {
                parser->consume_token();
                std::string field_type = parse_type(parser);
                
                // Verificar se é mutável
                bool is_mutable = false;
                if (parser->current_token().type == TokenType::MUT) {
                    is_mutable = true;
                    parser->consume_token();
                }
                
                auto field = std::make_unique<ClassFieldNode>(member_name, field_type, is_mutable);
                class_node->fields.push_back(std::move(field));
                
                // Exigir ; após o campo
                parser->expect(TokenType::SEMICOLON, "Expected ';' after field declaration");
            }
            // Se o próximo token for (, é um método
            else if (parser->current_token().type == TokenType::OPAREN) {
                // Parse da definição do método
                // Por enquanto, vamos pular o corpo do método
                parser->consume_token(); // consumir (
                
                // Pular parâmetros
                int paren_count = 1;
                while (paren_count > 0 && parser->current_token().type != TokenType::EOF_TOKEN) {
                    if (parser->current_token().type == TokenType::OPAREN) {
                        paren_count++;
                    } else if (parser->current_token().type == TokenType::CPAREN) {
                        paren_count--;
                    }
                    parser->consume_token();
                }
                
                // Pular corpo do método { ... }
                if (parser->current_token().type == TokenType::OBRACE) {
                    int brace_count = 1;
                    parser->consume_token(); // consumir {
                    
                    while (brace_count > 0 && parser->current_token().type != TokenType::EOF_TOKEN) {
                        if (parser->current_token().type == TokenType::OBRACE) {
                            brace_count++;
                        } else if (parser->current_token().type == TokenType::CBRACE) {
                            brace_count--;
                        }
                        parser->consume_token();
                    }
                }
                
                // Criar um nó de método vazio por enquanto
                auto method = std::make_unique<ClassMethodNode>(member_name, access_modifier, nullptr);
                class_node->methods.push_back(std::move(method));
            }
            else {
                // Erro: esperado : ou (
                parser->error("Expected ':' for field or '(' for method");
            }
        }
        else {
            // Pular tokens desconhecidos até encontrar ; ou }
            while (parser->current_token().type != TokenType::SEMICOLON && 
                   parser->current_token().type != TokenType::CBRACE &&
                   parser->current_token().type != TokenType::EOF_TOKEN) {
                parser->consume_token();
            }
            if (parser->current_token().type == TokenType::SEMICOLON) {
                parser->consume_token();
            }
        }
    }
    
    // Esperar }
    parser->expect(TokenType::CBRACE, "Expected '}' after class body");
    
    return std::move(class_node);
}
