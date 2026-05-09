#include "frontend/parser/statements/parse_class_stmt.hpp"
#include "frontend/parser/expressions/parse_primary_expr.hpp"
#include "frontend/parser/expressions/parse_type.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"

std::unique_ptr<Node> parse_class_stmt(Parser* parser) {
    parser->consume_token(); // consumir CLASS
    
    std::string class_name = parser->current_token().lexeme;
    parser->consume_token(); // consumir nome da classe
    
    auto class_node = std::make_unique<ClassStmtNode>(class_name);
    
    // Verificar herança
    if (parser->current_token().type == TokenType::EXTENDS) {
        parser->consume_token();
        class_node->parent_class = parser->current_token().lexeme;
        parser->consume_token();
    }

    // Verificar interfaces implementadas
    if (parser->current_token().type == TokenType::IMPLEMENTS) {
        parser->consume_token();
        while (parser->current_token().type == TokenType::IDENTIFIER) {
            class_node->implements_interfaces.push_back(parser->current_token().lexeme);
            parser->consume_token();
            if (parser->current_token().type == TokenType::COMMA)
                parser->consume_token();
            else
                break;
        }
    }

    parser->expect(TokenType::OBRACE, "Expected '{' after class name");
    
    // Parse do corpo da classe
    while (parser->current_token().type != TokenType::CBRACE && 
           parser->current_token().type != TokenType::EOF_TOKEN) {
        
        // Verificar modificador de acesso (privado por padrão)
        std::string access_modifier = "private";
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
        
        // Verificar se é um membro válido
        if (parser->current_token().type == TokenType::IDENTIFIER ||
            parser->current_token().type == TokenType::MUT ||
            parser->current_token().type == TokenType::NEW) {
            
            std::string member_name;
            bool is_mutable_field = false;
            
            // Se for MUT, é um campo mutável
            if (parser->current_token().type == TokenType::MUT) {
                is_mutable_field = true;
                parser->consume_token(); // consumir MUT
                // Próximo token deve ser o nome do campo
                if (parser->current_token().type == TokenType::IDENTIFIER) {
                    member_name = parser->current_token().lexeme;
                    parser->consume_token();
                }
            }
            // Se for IDENTIFIER ou NEW, é o nome diretamente
            else if (parser->current_token().type == TokenType::IDENTIFIER ||
                      parser->current_token().type == TokenType::NEW) {
                member_name = parser->current_token().lexeme;
                parser->consume_token();
            }
            
            // Campo
            if (parser->current_token().type == TokenType::COLON) {
                parser->consume_token();
                std::string field_type = parse_type(parser);
                
                auto field = std::make_unique<ClassFieldNode>(member_name, field_type, is_mutable_field);
                class_node->fields.push_back(std::move(field));
                parser->expect(TokenType::SEMICOLON, "Expected ';' after field declaration");
            }
            // Método
            else if (parser->current_token().type == TokenType::OPAREN) {
                // Parse parâmetros
                parser->consume_token(); // consumir (
                std::vector<std::pair<std::string, std::string>> params;
                
                while (parser->current_token().type != TokenType::CPAREN && 
                       parser->current_token().type != TokenType::EOF_TOKEN) {
                    if (parser->current_token().type == TokenType::IDENTIFIER) {
                        std::string param_name = parser->current_token().lexeme;
                        parser->consume_token();
                        
                        if (parser->current_token().type == TokenType::COLON) {
                            parser->consume_token();
                            std::string param_type = parse_type(parser);
                            params.push_back({param_name, param_type});
                        }
                        
                        if (parser->current_token().type == TokenType::COMMA) {
                            parser->consume_token();
                        }
                    }
                }
                
                parser->expect(TokenType::CPAREN, "Expected ')' after parameters");

                // Tipo de retorno opcional: ): tipo {
                std::string return_type = "void";
                if (parser->current_token().type == TokenType::COLON) {
                    parser->consume_token();
                    return_type = parse_type(parser);
                }

                // Parse corpo do método
                parser->expect(TokenType::OBRACE, "Expected '{' after method signature");

                // Converter params em ParamNodes
                std::vector<ParamNode> param_nodes;
                for (auto& [pname, ptype] : params) {
                    std::unordered_map<std::string, std::string> pm;
                    pm[pname] = ptype;
                    param_nodes.emplace_back(std::move(pm));
                }

                auto def_node = std::make_unique<FunctionStmtNode>(
                    member_name,
                    std::move(param_nodes),
                    return_type,
                    std::vector<std::unique_ptr<Stmt>>{}
                );

                while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
                    auto stmt = parse_stmt(parser);
                    if (stmt) {
                        def_node->body.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt.release())));
                    }
                }
                parser->expect(TokenType::CBRACE, "Expected '}'");

                auto method = std::make_unique<ClassMethodNode>(member_name, access_modifier, std::move(def_node));
                class_node->methods.push_back(std::move(method));
            }
            else {
                parser->error("Expected ':' for field or '(' for method");
                break;
            }
        }
        else {
            parser->error("Expected class member");
            break;
        }
    }
    
    parser->expect(TokenType::CBRACE, "Expected '}' after class body");
    return std::move(class_node);
}
