#include "frontend/parser/expressions/parse_closure_expr.hpp"
#include "frontend/ast/expressions/closure_expr_node.hpp"
#include "frontend/ast/statements/return_stmt_node.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/parser/statements/parse_block_util.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"

// Parse type identifier: int, str, |a: int|: int, etc.
static std::string parse_type_identifier(Parser* parser) {
    std::string type_str = "";
    
    if (parser->current_token().type == TokenType::BITWISE_OR) {
        // Function type: |params|: return_type
        type_str = "|";
        parser->consume_token(); // consume |
        
        // Parse parameters
        while (parser->current_token().type != TokenType::BITWISE_OR && parser->not_eof()) {
            std::string param_name = parser->expect(TokenType::IDENTIFIER, "Expected parameter name").lexeme;
            parser->expect(TokenType::COLON, "Expected ':' after parameter name");
            std::string param_type = parse_type_identifier(parser);
            
            type_str += param_name + ":" + param_type;
            
            if (parser->current_token().type == TokenType::COMMA) {
                type_str += ", ";
                parser->consume_token();
            }
        }
        
        parser->expect(TokenType::BITWISE_OR, "Expected '|' to close function type parameters");
        type_str += "|";
        
        // Parse return type
        parser->expect(TokenType::COLON, "Expected ':' after '|' in function type");
        std::string return_type = parse_type_identifier(parser);
        type_str += ":" + return_type;
        
        return type_str;
    } else {
        // Simple type: int, str, etc.
        type_str = parser->consume_token().lexeme;
        
        // Array type: int[10]
        if (parser->current_token().type == TokenType::OBRACKET) {
            parser->consume_token(); // [
            type_str += "[" + parser->consume_token().lexeme;
            parser->expect(TokenType::CBRACKET, "Expected ']' to close array type");
            type_str += "]";
        }
        
        return type_str;
    }
}

std::unique_ptr<Node> parse_closure_expr(Parser* parser) {
    auto node = std::make_unique<ClosureExprNode>();
    
    // Always start with |
    parser->expect(TokenType::BITWISE_OR, "Expected '|' to start closure");
    
    // Parse parameters (types are mandatory)
    while (parser->current_token().type != TokenType::BITWISE_OR && parser->not_eof()) {
        std::string param_name = parser->expect(TokenType::IDENTIFIER, "Expected parameter name").lexeme;
        
        // Type is mandatory: |x: int|
        parser->expect(TokenType::COLON, "Expected ':' after parameter name (types are mandatory in closures)");
        std::string param_type = parse_type_identifier(parser);
        
        node->parameters.emplace_back(param_name, param_type);
        
        if (parser->current_token().type == TokenType::COMMA) {
            parser->consume_token();
        } else if (parser->current_token().type != TokenType::BITWISE_OR) {
            parser->error("Expected ',' or '|' in closure parameters");
            return nullptr;
        }
    }
    
    // Close parameters
    parser->expect(TokenType::BITWISE_OR, "Expected '|' to close closure parameters");
    
    // Parse return type (optional: |x| body or |x|: int body)
    node->return_type = "auto"; // default: inferred
    
    if (parser->current_token().type == TokenType::COLON) {
        parser->consume_token(); // consume :
        
        // Check if return type is a function (nested closure)
        if (parser->current_token().type == TokenType::BITWISE_OR) {
            // This is a nested closure - parse it as the body expression
            auto nested_closure = parse_closure_expr(parser);
            if (nested_closure) {
                // Add the nested closure as a return statement in the body
                auto return_stmt = std::make_unique<ReturnStmtNode>(
                    std::unique_ptr<Expr>(static_cast<Expr*>(nested_closure.release()))
                );
                node->body.push_back(std::move(return_stmt));
                node->return_type = "auto"; // Infer from nested closure
                return node; // Skip normal body parsing
            } else {
                node->return_type = "auto"; // fallback
            }
        } else {
            node->return_type = parse_type_identifier(parser);
        }
    }
    
    // Parse body
    if (parser->current_token().type == TokenType::OBRACE) {
        // Block body
        parser->consume_token();
        node->body = parse_body(parser);
        parser->expect(TokenType::CBRACE, "Expected '}' to close closure body");
    } else {
        // Expression body
        auto expr = parse_expr(parser);
        auto expr_ptr = std::unique_ptr<Expr>(static_cast<Expr*>(expr.release()));
        node->body.push_back(std::make_unique<ReturnStmtNode>(std::move(expr_ptr)));
    }
    
    return node;
}
