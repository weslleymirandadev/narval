#include "frontend/parser/expressions/parse_bitwise_expr.hpp"
#include "frontend/parser/expressions/parse_relational_expr.hpp"

// The three bitwise levels sit between the logical operators (&&/||) and
// equality, the order C uses: `a & b == c` is `a & (b == c)` and
// `x && y | z` is `x && (y | z)`. `|` stays available as the closure
// delimiter because a binary level is only reached after an operand, while a
// closure is parsed where an operand is expected.

namespace {
std::unique_ptr<Node> parse_bitwise_level(
    Parser* parser,
    std::unique_ptr<Node> (*next)(Parser*),
    std::initializer_list<TokenType> ops
) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos =
        std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    auto left = next(parser);

    while (true) {
        bool matches = false;
        for (TokenType t : ops) {
            if (parser->current_token().type == t) { matches = true; break; }
        }
        if (!matches) break;

        std::string opToken = parser->consume_token().lexeme;
        auto right = next(parser);

        auto binaryNode = std::make_unique<BinaryExprNode>(
            opToken,
            std::unique_ptr<Expr>(static_cast<Expr*>(left.release())),
            std::unique_ptr<Expr>(static_cast<Expr*>(right.release()))
        );

        if (right && right->position) {
            pos->col[1] = right->position->col[1];
            pos->pos[1] = right->position->pos[1];
        }

        binaryNode->position = std::move(pos);
        left = std::move(binaryNode);
    }

    return left;
}
}  // namespace

std::unique_ptr<Node> parse_bitwise_or_expr(Parser* parser) {
    // In a `match` pattern `|` separates alternatives, so the or level steps aside
    // and the pattern parser handles it (see parse_match_stmt).
    if (parser->in_match_pattern) return parse_bitwise_xor_expr(parser);
    return parse_bitwise_level(parser, parse_bitwise_xor_expr, { TokenType::BITWISE_OR });
}

std::unique_ptr<Node> parse_bitwise_xor_expr(Parser* parser) {
    return parse_bitwise_level(parser, parse_bitwise_and_expr, { TokenType::BITWISE_XOR });
}

std::unique_ptr<Node> parse_bitwise_and_expr(Parser* parser) {
    return parse_bitwise_level(parser, parse_relational_expr, { TokenType::BITWISE_AND });
}
