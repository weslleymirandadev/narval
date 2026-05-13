#include "frontend/parser/expressions/parse_closure_expr.hpp"
#include "frontend/ast/expressions/closure_expr_node.hpp"
#include "frontend/ast/statements/return_stmt_node.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/parser/statements/parse_block_util.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"

static bool split_function_type(const std::string& type_str,
                                std::vector<std::pair<std::string, std::string>>& params,
                                std::string& ret_type) {
    if (type_str.empty() || type_str[0] != '|') return false;

    size_t close = type_str.find('|', 1);
    if (close == std::string::npos || close + 1 >= type_str.size() || type_str[close + 1] != ':') {
        return false;
    }

    std::string param_list = type_str.substr(1, close - 1);
    ret_type = type_str.substr(close + 2);

    size_t start = 0;
    while (start < param_list.size()) {
        size_t comma = param_list.find(',', start);
        std::string part = param_list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        while (!part.empty() && part.front() == ' ') part.erase(part.begin());
        while (!part.empty() && part.back() == ' ') part.pop_back();

        if (!part.empty()) {
            size_t colon = part.find(':');
            if (colon == std::string::npos) return false;
            std::string name = part.substr(0, colon);
            std::string ty = part.substr(colon + 1);
            while (!name.empty() && name.front() == ' ') name.erase(name.begin());
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!ty.empty() && ty.front() == ' ') ty.erase(ty.begin());
            while (!ty.empty() && ty.back() == ' ') ty.pop_back();
            params.emplace_back(name, ty);
        }

        if (comma == std::string::npos) break;
        start = comma + 1;
    }

    return true;
}

static bool body_returns_closure(const CodeBlock& body) {
    for (const auto& stmt : body) {
        if (!stmt || stmt->kind != NodeType::ReturnStatement) continue;
        auto* ret = static_cast<ReturnStmtNode*>(stmt.get());
        if (ret->value && ret->value->kind == NodeType::ClosureExpression) return true;
    }
    return false;
}

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
        node->return_type = parse_type_identifier(parser);
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

    // Sugar for currying:
    // |a: int|: |b: int|: int { return a + b; }
    // becomes |a: int| { return |b: int|: int { return a + b; }; }
    if (node->return_type != "auto" && node->body.size() == 1 && !body_returns_closure(node->body)) {
        std::vector<std::pair<std::string, std::string>> nested_params;
        std::string nested_return_type;
        if (split_function_type(node->return_type, nested_params, nested_return_type)) {
            auto nested = std::make_unique<ClosureExprNode>();
            nested->parameters = std::move(nested_params);
            nested->return_type = nested_return_type;
            nested->body = std::move(node->body);
            if (node->position) nested->position = std::make_unique<PositionData>(*node->position);

            auto return_stmt = std::make_unique<ReturnStmtNode>(std::move(nested));
            node->body.push_back(std::move(return_stmt));
        }
    }
    
    return node;
}
