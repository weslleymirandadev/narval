#include "frontend/parser/statements/parse_match_stmt.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"
#include "frontend/parser/expressions/parse_primary_expr.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include <iostream>

std::unique_ptr<Node> parse_match_stmt(Parser* parser) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);
    
    parser->consume_token(); //match
    auto target = std::unique_ptr<Expr>(static_cast<Expr*>(parse_expr(parser).release()));

    parser->expect(TokenType::OBRACE, "Expected '{'.");

    std::vector<std::unique_ptr<Expr>> cases;
    std::vector<CodeBlock> bodies;

    auto match = std::make_unique<MatchStmtNode>(std::move(target), std::move(cases), std::move(bodies));

    while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
        auto expr = parse_primary_expr(parser);
        parser->expect(TokenType::ARROW, "Expected '=>'.");
        parser->expect(TokenType::OBRACE, "Expected '{'.");
        CodeBlock body; //tu descobriu? ainda nao
        while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE){
            auto stmt_node = parse_stmt(parser);
            auto* stmt_ptr = dynamic_cast<Stmt*>(stmt_node.get());
            body.push_back(std::unique_ptr<Stmt>(stmt_ptr));
            stmt_node.release();
        }
        parser->consume_token();
        match->cases.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(expr.release())));
        match->bodies.push_back(std::move(body));
        if (parser->current_token().type == TokenType::COMMA) {
            parser->consume_token();
        }
    }
    auto tok = parser->expect(TokenType::CBRACE, "Expected '}'.");
    pos->col[2] = tok.column_end;
    pos->pos[2] = tok.position_end; // vou dormir mano, to morrendo tudo bem, vai la, ninguem é d ferrokkkkkk flww boa noite
    
    match->position = std::move(pos);
    return match;
    // ja volto    ok
}