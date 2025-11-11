#include "frontend/parser/expressions/parse_primary_expr.hpp"
#include "frontend/parser/expressions/parse_logical_expr.hpp"
#include "frontend/parser/expressions/parse_arguments_list.hpp"
#include "frontend/parser/expressions/parse_array_map_expr.hpp"
#include "frontend/parser/expressions/parse_vector_expr.hpp"
#include "frontend/parser/expressions/parse_boolean_literal.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include <cctype>

std::unique_ptr<Node> parse_primary_expr(Parser* parser) {
    TokenType type = parser->current_token().type;

    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    std::unique_ptr<Node> expr;
    switch (type) {
        case TokenType::NUMBER: {
            Token numToken = parser->consume_token();
            auto node = std::make_unique<NumericLiteralNode>(numToken.lexeme);
            node->position = std::move(pos);
            expr = std::move(node);
            break;
        }
        case TokenType::IDENTIFIER: {
            Token idToken = parser->consume_token();
            auto node = std::make_unique<IdentifierNode>(idToken.lexeme);
            node->position = std::move(pos);
            expr = std::move(node);
            break;
        }
        case TokenType::STRING: {
            Token strToken = parser->consume_token();
            const std::string& s = strToken.lexeme;
            size_t i = 0;
            std::vector<std::unique_ptr<Expr>> parts;
            std::string litbuf;

            auto flush_literal = [&]() {
                if (!litbuf.empty()) {
                    auto litNode = std::make_unique<StringLiteralNode>(litbuf);
                    parts.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(litNode.release())));
                    litbuf.clear();
                }
            };

            while (i < s.size()) {
                char c = s[i];
                if (c == '\\') {
                    // Escape next char if present for braces
                    if (i + 1 < s.size() && (s[i + 1] == '{' || s[i + 1] == '}')) {
                        litbuf.push_back(s[i + 1]);
                        i += 2;
                        continue;
                    }
                    // Keep backslash as-is for other sequences (lexer may have preserved it)
                    litbuf.push_back('\\');
                    i += 1;
                    continue;
                }
                if (c == '{') {
                    // Find matching unescaped '}'
                    size_t j = i + 1;
                    bool found = false;
                    while (j < s.size()) {
                        if (s[j] == '\\') { j += 2; continue; }
                        if (s[j] == '}') { found = true; break; }
                        ++j;
                    }
                    if (!found) {
                        // Unclosed: treat as literal
                        litbuf.append(s.substr(i));
                        i = s.size();
                        break;
                    }
                    // Extract inside and handle escapes inside as raw (escapes are not special for ident)
                    std::string inside = s.substr(i + 1, j - (i + 1));
                    // trim
                    size_t l = 0, r = inside.size();
                    while (l < r && std::isspace(static_cast<unsigned char>(inside[l]))) ++l;
                    while (r > l && std::isspace(static_cast<unsigned char>(inside[r - 1]))) --r;
                    std::string ident = inside.substr(l, r - l);
                    bool valid = !ident.empty() && (std::isalpha(static_cast<unsigned char>(ident[0])) || ident[0] == '_');
                    for (size_t k = 1; valid && k < ident.size(); ++k) {
                        char ch = ident[k];
                        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) valid = false;
                    }
                    if (valid) {
                        flush_literal();
                        auto idNode = std::make_unique<IdentifierNode>(ident);
                        parts.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(idNode.release())));
                    } else {
                        // Keep as literal with braces
                        litbuf.append(s.substr(i, j - i + 1));
                    }
                    i = j + 1;
                    continue;
                }
                // default: accumulate literal
                litbuf.push_back(c);
                ++i;
            }
            flush_literal();

            if (parts.empty()) {
                auto node = std::make_unique<StringLiteralNode>(std::string());
                node->position = std::move(pos);
                expr = std::move(node);
            } else if (parts.size() == 1) {
                // single part: return directly
                parts[0]->position = std::move(pos);
                expr = std::unique_ptr<Node>(static_cast<Node*>(parts[0].release()));
            } else {
                // Build left-associative concatenation chain using '+'
                std::unique_ptr<Expr> accum = std::move(parts[0]);
                for (size_t idx = 1; idx < parts.size(); ++idx) {
                    auto bin = std::make_unique<BinaryExprNode>("+", std::move(accum), std::move(parts[idx]));
                    accum = std::move(bin);
                }
                accum->position = std::move(pos);
                expr = std::unique_ptr<Node>(static_cast<Node*>(accum.release()));
            }
            break;
        }
        case TokenType::OPAREN: {
            parser->consume_token();
            
            // Check if it's a tuple
            if (parser->current_token().type == TokenType::CPAREN) {
                // Empty tuple: ()
                parser->expect(TokenType::CPAREN, "Expected ')'.");
                auto tuple_node = std::make_unique<TupleExprNode>(std::vector<std::unique_ptr<Expr>>{});
                tuple_node->position = std::move(pos);
                expr = std::move(tuple_node);
                break;
            }
            
            // Try to parse first expression
            auto first_expr = parse_logical_expr(parser);
            
            // Check if there's a comma, indicating a tuple
            if (parser->current_token().type == TokenType::COMMA) {
                // It's a tuple with at least one element
                std::vector<std::unique_ptr<Expr>> elements;
                elements.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(first_expr.release())));
                
                // Parse remaining elements
                while (parser->current_token().type == TokenType::COMMA) {
                    parser->consume_token(); // consume ','
                    auto element = parse_logical_expr(parser);
                    elements.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(element.release())));
                }
                
                parser->expect(TokenType::CPAREN, "Expected ')'.");
                
                auto tuple_node = std::make_unique<TupleExprNode>(std::move(elements));
                tuple_node->position = std::move(pos);
                expr = std::move(tuple_node);
                break;
            }
            
            // It's a parenthesized expression
            parser->expect(TokenType::CPAREN, "Expected ')'.");
            if (first_expr && first_expr->position) {
                pos->col[1] = first_expr->position->col[1];
                pos->pos[1] = first_expr->position->pos[1];
            }
            first_expr->position = std::move(pos);
            expr = std::move(first_expr);
            break;
        }
        case TokenType::OBRACKET: {
            auto vector_node = parse_vector_expr(parser);
            if (vector_node && vector_node->position) {
                pos->col[1] = vector_node->position->col[1];
                pos->pos[1] = vector_node->position->pos[1];
            }
            vector_node->position = std::move(pos);
            expr = std::move(vector_node);
            break;
        }
        case TokenType::OBRACE: {
            auto array_map_node = parse_array_map_expr(parser);
            if (array_map_node && array_map_node->position) {
                pos->col[1] = array_map_node->position->col[1];
                pos->pos[1] = array_map_node->position->pos[1];
            }
            array_map_node->position = std::move(pos);
            expr = std::move(array_map_node);
            break;
        }
        case TokenType::TRUE:
        case TokenType::FALSE:
            expr = parse_boolean_literal(parser);
            break;
        default:
            parser->error("Unexpected token in primary expression: '" + parser->current_token().lexeme + "'");
            return nullptr;
    }
    return expr;
}