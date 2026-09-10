#include "frontend/parser/statements/parse_comptime_stmt.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/parser/statements/parse_function_stmt.hpp"
#include "frontend/parser/statements/parse_block_util.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"
#include "frontend/parser/expressions/parse_range_expr.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"

namespace {

// comptime def name(params): ret { ... }
std::unique_ptr<Node> parse_comptime_func(Parser* parser) {
    auto fn_node = parse_function_stmt(parser);
    if (!fn_node || fn_node->kind != NodeType::FunctionStatement) return fn_node;
    auto* fn = static_cast<FunctionStmtNode*>(fn_node.get());

    CodeBlock body = std::move(fn->body);
    auto node = std::make_unique<ComptimeFuncNode>(
        fn->name, std::move(fn->parameters), fn->return_type, std::move(body));
    if (fn->position) node->position = std::make_unique<PositionData>(*fn->position);
    return node;
}

// comptime for VAR in <iterable> { ... }
std::unique_ptr<Node> parse_comptime_for(Parser* parser) {
    auto kw = parser->current_token();
    parser->consume_token(); // 'for'
    std::string var = parser->expect(TokenType::IDENTIFIER, "Expected loop variable after 'comptime for'").lexeme;
    parser->expect(TokenType::IN, "Expected 'in' after loop variable");
    auto range = parse_range_expr(parser);  // 0..4 / 0..=4 / iterable
    parser->expect(TokenType::OBRACE, "Expected '{' to open comptime for body");
    CodeBlock body = parse_body(parser);
    parser->expect(TokenType::CBRACE, "Expected '}' to close comptime for body");

    auto node = std::make_unique<ComptimeForNode>(
        var,
        range ? std::unique_ptr<Expr>(static_cast<Expr*>(range.release())) : nullptr,
        std::move(body));
    node->position = std::make_unique<PositionData>(
        kw.line, kw.column_start, kw.column_end, kw.position_start, kw.position_end, kw.filename);
    return node;
}

// comptime if COND { ... } [else { ... }]
std::unique_ptr<Node> parse_comptime_if(Parser* parser) {
    auto kw = parser->current_token();
    parser->consume_token(); // 'if'
    auto cond = parse_expr(parser);
    parser->expect(TokenType::OBRACE, "Expected '{' to open comptime if body");
    CodeBlock then_body = parse_body(parser);
    parser->expect(TokenType::CBRACE, "Expected '}' to close comptime if body");

    CodeBlock else_body;
    if (parser->current_token().type == TokenType::ELSE) {
        parser->consume_token();
        parser->expect(TokenType::OBRACE, "Expected '{' after 'else'");
        else_body = parse_body(parser);
        parser->expect(TokenType::CBRACE, "Expected '}' to close comptime else body");
    }

    auto node = std::make_unique<ComptimeIfNode>(
        cond ? std::unique_ptr<Expr>(static_cast<Expr*>(cond.release())) : nullptr,
        std::move(then_body), std::move(else_body));
    node->position = std::make_unique<PositionData>(
        kw.line, kw.column_start, kw.column_end, kw.position_start, kw.position_end, kw.filename);
    return node;
}

// comptime while COND { ... } / inline while COND { ... }
std::unique_ptr<Node> parse_comptime_while(Parser* parser) {
    auto kw = parser->current_token();
    parser->consume_token(); // 'while'
    auto cond = parse_expr(parser);
    parser->expect(TokenType::OBRACE, "Expected '{' to open unrolled while body");
    CodeBlock body = parse_body(parser);
    parser->expect(TokenType::CBRACE, "Expected '}' to close unrolled while body");

    auto node = std::make_unique<ComptimeWhileNode>(
        cond ? std::unique_ptr<Expr>(static_cast<Expr*>(cond.release())) : nullptr,
        std::move(body));
    node->position = std::make_unique<PositionData>(
        kw.line, kw.column_start, kw.column_end, kw.position_start, kw.position_end, kw.filename);
    return node;
}

} // anonymous namespace

std::unique_ptr<Node> parse_inline_stmt(Parser* parser) {
    parser->consume_token(); // 'inline'
    switch (parser->current_token().type) {
        case TokenType::FOR:
            return parse_comptime_for(parser);
        case TokenType::WHILE:
            return parse_comptime_while(parser);
        default:
            parser->error("Expected 'for' or 'while' after 'inline'");
            return nullptr;
    }
}

std::unique_ptr<Node> parse_comptime_stmt(Parser* parser) {
    auto kw = parser->current_token();
    parser->consume_token(); // 'comptime'

    auto with_pos = [&](std::unique_ptr<Node> node) -> std::unique_ptr<Node> {
        if (node && !node->position) {
            node->position = std::make_unique<PositionData>(
                kw.line, kw.column_start, kw.column_end,
                kw.position_start, kw.position_end, kw.filename);
        }
        return node;
    };

    switch (parser->current_token().type) {
        case TokenType::DEF:
            return with_pos(parse_comptime_func(parser));
        case TokenType::FOR:
            return with_pos(parse_comptime_for(parser));
        case TokenType::IF:
            return with_pos(parse_comptime_if(parser));
        case TokenType::OBRACE: {
            parser->consume_token();
            CodeBlock body = parse_body(parser);
            parser->expect(TokenType::CBRACE, "Expected '}' to close comptime block");
            return with_pos(std::make_unique<ComptimeBlockNode>(std::move(body)));
        }
        default:
            break;
    }

    // comptime NAME = <expr>
    if (parser->current_token().type == TokenType::IDENTIFIER &&
        parser->next_token().type == TokenType::ASSIGNMENT) {
        std::string name = parser->consume_token().lexeme;
        parser->consume_token(); // '='
        auto value = parse_expr(parser);
        if (parser->current_token().type == TokenType::SEMICOLON) parser->consume_token();
        auto node = std::make_unique<ComptimeDeclNode>(
            name,
            value ? std::unique_ptr<Expr>(static_cast<Expr*>(value.release())) : nullptr);
        return with_pos(std::move(node));
    }

    // comptime <expr>
    auto inner = parse_expr(parser);
    if (parser->current_token().type == TokenType::SEMICOLON) parser->consume_token();
    auto node = std::make_unique<ComptimeExprNode>(
        inner ? std::unique_ptr<Expr>(static_cast<Expr*>(inner.release())) : nullptr);
    return with_pos(std::move(node));
}
