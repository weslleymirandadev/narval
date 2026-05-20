#include "frontend/parser/statements/parse_inline_asm_stmt.hpp"
#include "frontend/ast/statements/inline_asm_stmt_node.hpp"

// Gramática:
//   asm { "template" } input { (reg VAR;)* } output { (VAR -> VAR;)* }
std::unique_ptr<Node> parse_inline_asm_stmt(Parser* parser) {
    auto node = std::make_unique<InlineAsmStmtNode>();
    if (node->position == nullptr && parser->current_token().line > 0) {
        auto t = parser->current_token();
        node->position = std::make_unique<PositionData>(
            t.line, t.column_start, t.column_end,
            t.position_start, t.position_end, t.filename);
    }

    parser->expect(TokenType::ASM, "Expected 'asm'");
    parser->expect(TokenType::OBRACE, "Expected '{'");

    if (parser->current_token().type != TokenType::STRING) {
        parser->error("Expected assembly template string inside 'asm { }'");
        return nullptr;
    }
    node->asm_template = parser->current_token().lexeme;
    parser->consume_token();

    parser->expect(TokenType::CBRACE, "Expected '}'");

    // input { reg a; reg b; }
    if (parser->current_token().type != TokenType::IDENTIFIER ||
        parser->current_token().lexeme != "input") {
        parser->error("Expected 'input' block after asm template");
        return nullptr;
    }
    parser->consume_token(); // consume 'input'
    parser->expect(TokenType::OBRACE, "Expected '{'");

    while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
        // constraint kind: 'reg'
        if (parser->current_token().type != TokenType::IDENTIFIER ||
            parser->current_token().lexeme != "reg") {
            parser->error("Expected 'reg' constraint in input block");
            return nullptr;
        }
        parser->consume_token(); // consume 'reg'

        AsmInputConstraint c;
        c.kind     = AsmConstraintKind::Reg;
        c.var_name = parser->expect(TokenType::IDENTIFIER, "Expected variable name").lexeme;
        parser->expect(TokenType::SEMICOLON, "Expected ';'");
        node->inputs.push_back(c);
    }
    parser->expect(TokenType::CBRACE, "Expected '}'");

    // output { a -> result; }
    if (parser->current_token().type != TokenType::IDENTIFIER ||
        parser->current_token().lexeme != "output") {
        parser->error("Expected 'output' block after input block");
        return nullptr;
    }
    parser->consume_token(); // consume 'output'
    parser->expect(TokenType::OBRACE, "Expected '{'");

    while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
        AsmOutputBinding b;
        b.from_var   = parser->expect(TokenType::IDENTIFIER, "Expected source variable").lexeme;
        parser->expect(TokenType::ARROW, "Expected '->'");
        b.result_var = parser->expect(TokenType::IDENTIFIER, "Expected result variable name").lexeme;
        parser->expect(TokenType::SEMICOLON, "Expected ';'");
        node->outputs.push_back(b);
    }
    parser->expect(TokenType::CBRACE, "Expected '}'");

    return node;
}
