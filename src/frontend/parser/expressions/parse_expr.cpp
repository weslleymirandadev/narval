#include "frontend/parser/statements/parse_declaration_stmt.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"
#include "frontend/parser/expressions/parse_or_expr.hpp"
#include "frontend/parser/expressions/parse_primary_expr.hpp"
#include "frontend/parser/expressions/parse_access_expr.hpp"
#include "frontend/parser/expressions/parse_call_member_expr.hpp"
#include "frontend/parser/expressions/parse_unary_expr.hpp"
#include "frontend/parser/expressions/parse_additive_expr.hpp"
#include "frontend/parser/expressions/parse_logical_expr.hpp"
#include "frontend/parser/expressions/parse_equality_expr.hpp"
#include "frontend/parser/expressions/parse_relational_expr.hpp"
#include "frontend/parser/expressions/parse_assignment_expr.hpp"
#include "frontend/parser/expressions/parse_vector_expr.hpp"
#include "frontend/parser/expressions/parse_array_map_expr.hpp"
#include "frontend/parser/expressions/parse_closure_expr.hpp"
#include "frontend/parser/expressions/parse_type.hpp"
#include "frontend/parser/expressions/parse_generic_ctor.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/new_expr_node.hpp"

// Lookahead: returns true if the current position looks like ClassName<TypeArgs>(
// e.g. Box<int>(42) or Pair<int, str>(a, b)
bool is_generic_ctor_start(Parser* p) {
    if (p->current_token().type != TokenType::IDENTIFIER) return false;
    if (p->peek_at(1).type != TokenType::LT) return false;
    int depth = 1;
    size_t i = 2;
    while (true) {
        auto tok = p->peek_at(i);
        if (tok.type == TokenType::EOF_TOKEN) return false;
        if (tok.type == TokenType::LT) { depth++; i++; continue; }
        if (tok.type == TokenType::GT) {
            i++;
            if (--depth == 0) break;
            continue;
        }
        if (tok.type == TokenType::IDENTIFIER || tok.type == TokenType::COMMA ||
            tok.type == TokenType::NONE_KW) { i++; continue; }
        return false;
    }
    return p->peek_at(i).type == TokenType::OPAREN;
}

// Parses ClassName<TypeArgs>(args) as a NewExprNode (constructor call without 'new')
std::unique_ptr<Node> parse_generic_ctor_call(Parser* parser) {
    auto class_name_token = parser->consume_token(); // ClassName
    std::string base_name = class_name_token.lexeme;
    std::string class_name = base_name;

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

    auto new_expr = std::make_unique<NewExprNode>(class_name, base_name);
    new_expr->position = std::make_unique<PositionData>(
        class_name_token.line,
        class_name_token.column_start, class_name_token.column_end,
        class_name_token.position_start, class_name_token.position_end,
        class_name_token.filename
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

    return new_expr;
}

std::unique_ptr<Node> parse_expr(Parser* parser) {
    // Closures têm a precedência mais alta
    if (parser->current_token().type == TokenType::BITWISE_OR ||
        parser->current_token().type == TokenType::OR ||
        is_generic_closure_start(parser)) {
        return parse_closure_expr(parser);
    }
    
    if (parser->current_token().type == TokenType::OBRACKET) {
        return parse_vector_expr(parser);
    }
    
    if (parser->current_token().type == TokenType::OBRACE) {
        return parse_array_map_expr(parser);
    }

    if (parser->current_token().type == TokenType::MINUS ||
        parser->current_token().type == TokenType::NOT ||
        parser->current_token().type == TokenType::INCREMENT ||
        parser->current_token().type == TokenType::DECREMENT ||
        parser->next_token().type == TokenType::INCREMENT ||
        parser->next_token().type == TokenType::DECREMENT
    ) {
        return parse_unary_expr(parser);
    }
    
    // If the next token starts a postfix chain (call, member, access),
    // parse a primary and delegate to the unified call/member/access parser.
    if (
        parser->next_token().type == TokenType::OPAREN ||
        parser->next_token().type == TokenType::OBRACKET ||
        parser->next_token().type == TokenType::DOT
    ) {
        std::unique_ptr<Node> expr = parse_primary_expr(parser);
        expr = parse_call_member_expr(parser, std::move(expr));

        // Handle assignment to member/call result: obj.field = value
        if (parser->current_token().type == TokenType::ASSIGNMENT ||
            parser->current_token().type == TokenType::PLUS_ASSIGN ||
            parser->current_token().type == TokenType::MINUS_ASSIGN ||
            parser->current_token().type == TokenType::MUL_ASSIGN ||
            parser->current_token().type == TokenType::DIV_ASSIGN ||
            parser->current_token().type == TokenType::INTEGER_DIV_ASSIGN ||
            parser->current_token().type == TokenType::POWER_ASSIGN ||
            parser->current_token().type == TokenType::MOD_ASSIGN ||
            parser->current_token().type == TokenType::BITWISE_AND_ASSIGN ||
            parser->current_token().type == TokenType::BITWISE_OR_ASSIGN ||
            parser->current_token().type == TokenType::BITWISE_XOR_ASSIGN ||
            parser->current_token().type == TokenType::LEFT_SHIFT_ASSIGN ||
            parser->current_token().type == TokenType::RIGHT_SHIFT_ASSIGN ||
            parser->current_token().type == TokenType::AT_ASSIGN) {

            std::string assign_op = parser->consume_token().lexeme;
            auto value = parse_logical_expr(parser);
            parser->expect(TokenType::SEMICOLON, "Expected ';'.");

            return std::make_unique<AssignmentExprNode>(
                std::unique_ptr<Expr>(static_cast<Expr*>(expr.release())),
                assign_op,
                std::unique_ptr<Expr>(static_cast<Expr*>(value.release()))
            );
        }

        return try_parse_or(parser, std::move(std::unique_ptr<Expr>(static_cast<Expr*>(expr.release()))));
    }

    if (parser->next_token().type == TokenType::COLON) {
        return parse_declaration_stmt(parser, false);
    }

    // Generic constructor call without 'new': ClassName<T>(args)
    if (is_generic_ctor_start(parser)) {
        return parse_generic_ctor_call(parser);
    }

    switch (parser->next_token().type) {
        case TokenType::AND:
        case TokenType::OR:
        case TokenType::EQUALS:
        case TokenType::DIFFERENT:
        case TokenType::LESS_THAN_EQUALS:
        case TokenType::GREATER_THAN_EQUALS:
        case TokenType::LT:
        case TokenType::GT:
        case TokenType::PLUS:
        case TokenType::MINUS:
        case TokenType::MUL:
        case TokenType::DIV:
        case TokenType::MOD:
        case TokenType::POWER:
        case TokenType::INTEGER_DIV: {
            return parse_logical_expr(parser);
        }
        default: return parse_assignment_expr(parser);
    }
}