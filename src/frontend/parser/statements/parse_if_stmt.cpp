#include "frontend/parser/statements/parse_if_stmt.hpp"
#include "frontend/parser/expressions/parse_logical_expr.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/parser/statements/parse_block_util.hpp"

std::unique_ptr<Node> parse_if_stmt(Parser* parser) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    parser->consume_token(); // 'if'

    auto condition = parse_logical_expr(parser);


    auto if_node = std::make_unique<IfStatementNode>(
        std::unique_ptr<Expr>(static_cast<Expr*>(condition.release())),
        std::vector<std::unique_ptr<Stmt>>{},
        std::vector<std::unique_ptr<Stmt>>{}
    );

    if_node->consequent = parse_block_or_inline(parser);

    // The chain (`elif`, `else if`, `else`) desugars into the alternate as ONE nested if:
    //     if a { A } elif b { B } else { C }   ->   if a { A } else { if b { B } else { C } }
    //
    // It used to be pushed FLAT into a single `alternate` vector, mixing nested if nodes with the
    // final else's plain statements. The codegen emits `alternate` as the else BODY, so the else's
    // statements ran right after the elif — unconditionally. Measured: with x == 2, the chain
    // `if x == 1 { "um" } elif x == 2 { "dois" } else { "outro" }` printed BOTH "dois" and "outro",
    // and the `else if` spelling had the same defect.
    struct Branch { std::unique_ptr<Node> cond; std::vector<std::unique_ptr<Stmt>> body; };
    std::vector<Branch> branches;
    std::vector<std::unique_ptr<Stmt>> final_else;
    bool has_final_else = false;

    while (parser->not_eof() && parser->current_token().type == TokenType::ELIF) {
        parser->consume_token(); // 'elif'
        Branch br;
        br.cond = parse_logical_expr(parser);
        br.body = parse_block_or_inline(parser);
        branches.push_back(std::move(br));
    }

    // `else if` and the final `else` are the same thing in two spellings; both end the chain.
    while (parser->not_eof() && parser->current_token().type == TokenType::ELSE) {
        parser->consume_token(); // 'else'

        if (parser->current_token().type == TokenType::IF) {
            parser->consume_token(); // 'if'
            Branch br;
            br.cond = parse_logical_expr(parser);
            br.body = parse_block_or_inline(parser);
            branches.push_back(std::move(br));
            continue;
        }

        final_else = parse_block_or_inline(parser);
        has_final_else = true;
        break;
    }

    // Build the chain from the inside out: the last branch takes the final else, and each branch
    // takes the next one as its alternate.
    std::vector<std::unique_ptr<Stmt>> alternate =
        has_final_else ? std::move(final_else) : std::vector<std::unique_ptr<Stmt>>{};
    for (auto it = branches.rbegin(); it != branches.rend(); ++it) {
        auto nested = std::make_unique<IfStatementNode>(
            std::unique_ptr<Expr>(static_cast<Expr*>(it->cond.release())),
            std::move(it->body),
            std::move(alternate));
        alternate.clear();
        alternate.push_back(std::move(nested));
    }
    if_node->alternate = std::move(alternate);

    if (if_node && if_node->position) {
        pos->col[1] = if_node->position->col[1];
        pos->pos[1] = if_node->position->pos[1];
    }

    if_node->position = std::move(pos);

    return if_node;
}