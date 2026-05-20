#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/parser/statements/parse_function_stmt.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"
#include "frontend/parser/statements/parse_if_stmt.hpp"
#include "frontend/parser/statements/parse_return_stmt.hpp"
#include "frontend/parser/statements/parse_break_stmt.hpp"
#include "frontend/parser/statements/parse_continue_stmt.hpp"
#include "frontend/parser/statements/parse_for_stmt.hpp"
#include "frontend/parser/statements/parse_forever_stmt.hpp"
#include "frontend/parser/statements/parse_while_stmt.hpp"
#include "frontend/parser/statements/parse_match_stmt.hpp"
#include "frontend/parser/statements/parse_declaration_stmt.hpp"
#include "frontend/parser/statements/parse_class_stmt.hpp"
#include "frontend/parser/statements/parse_interface_stmt.hpp"
#include "frontend/parser/statements/parse_try_stmt.hpp"
#include "frontend/parser/statements/parse_throw_stmt.hpp"
#include "frontend/parser/statements/parse_enum_stmt.hpp"
#include "frontend/parser/statements/parse_defer_error_stmt.hpp"
#include "frontend/parser/statements/parse_extern_stmt.hpp"
#include "frontend/parser/statements/parse_extern_from_import_stmt.hpp"
#include "frontend/parser/statements/parse_import_stmt.hpp"
#include "frontend/parser/statements/parse_attribute_stmt.hpp"
#include "frontend/parser/statements/parse_decorator_stmt.hpp"
#include "frontend/parser/statements/parse_inline_asm_stmt.hpp"
#include "frontend/ast/statements/attribute_stmt_node.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"

std::unique_ptr<Node> parse_stmt(Parser* parser) {
    switch (parser->current_token().type) {
        case TokenType::AT: {
            if (parser->next_token().type == TokenType::OBRACKET) {
                auto attr_node = parse_attribute_stmt(parser);
                auto* attr = static_cast<AttributeStmtNode*>(attr_node.get());

                // Apenas absorver o atributo quando tem 'abi' — injetar na função seguinte.
                // Para todos os outros (@[no_std], @[strip], etc.) retornar normalmente
                // para que o attribute_mapper os encontre no corpo do programa.
                if (attr && attr->has_attr("abi")) {
                    auto next_stmt = parse_stmt(parser);
                    if (next_stmt && next_stmt->kind == NodeType::FunctionStatement) {
                        auto* fn = static_cast<FunctionStmtNode*>(next_stmt.get());
                        fn->abi = attr->get_arg("abi", 0);
                        fn->is_low_level = true;
                    }
                    if (next_stmt) return next_stmt;
                }

                return attr_node;
            } else {
                auto node = parse_decorator_stmt(parser);
                if (node) return node;
            }
            break;
        }
        case TokenType::MUT:
            parser->consume_token();
            return parse_declaration_stmt(parser, true);
        case TokenType::ABSTRACT:
        case TokenType::CLASS: return parse_class_stmt(parser);
        case TokenType::INTERFACE: return parse_interface_stmt(parser);
        case TokenType::ENUM: return parse_enum_stmt(parser);
        case TokenType::DEF: return parse_function_stmt(parser);
        case TokenType::ASM: return parse_inline_asm_stmt(parser);
        case TokenType::NAKED_ASM: {
            // naked_asm def NAME: asm { return `body`; }
            // Resultado: FunctionStmtNode com is_naked_asm=true e naked_asm_body preenchido.
            // Checker e demais passes tratam como função normal.
            auto tok = parser->current_token();
            parser->consume_token(); // naked_asm
            parser->expect(TokenType::DEF, "Expected 'def' after 'naked_asm'");
            std::string fn_name = parser->expect(TokenType::IDENTIFIER, "Expected function name").lexeme;
            parser->expect(TokenType::COLON, "Expected ':'");
            parser->expect(TokenType::ASM,   "Expected 'asm'");
            parser->expect(TokenType::OBRACE, "Expected '{'");
            parser->expect(TokenType::RETURN, "Expected 'return'");
            if (parser->current_token().type != TokenType::STRING) {
                parser->error("Expected backtick string with assembly body after 'return'");
                return nullptr;
            }
            std::string asm_body = parser->current_token().lexeme;
            parser->consume_token();
            parser->expect(TokenType::SEMICOLON, "Expected ';'");
            parser->expect(TokenType::CBRACE, "Expected '}'");

            auto fn = std::make_unique<FunctionStmtNode>(
                fn_name, std::vector<ParamNode>{}, "None",
                std::vector<std::unique_ptr<Stmt>>{});
            fn->is_naked_asm  = true;
            fn->naked_asm_body = asm_body;
            fn->position = std::make_unique<PositionData>(
                tok.line, tok.column_start, tok.column_end,
                tok.position_start, tok.position_end, tok.filename);
            return fn;
        }
        case TokenType::IF: return parse_if_stmt(parser);
        case TokenType::RETURN: return parse_return_stmt(parser);
        case TokenType::BREAK: return parse_break_stmt(parser);
        case TokenType::CONTINUE: return parse_continue_stmt(parser);
        case TokenType::FOR: return parse_for_stmt(parser);
        case TokenType::FOREVER: return parse_forever_stmt(parser);
        case TokenType::WHILE: return parse_while_stmt(parser);
        case TokenType::MATCH: return parse_match_stmt(parser);
        case TokenType::TRY: return parse_try_stmt(parser);
        case TokenType::THROW: return parse_throw_stmt(parser);
        case TokenType::DEFER: return parse_defer_error_stmt(parser);
        case TokenType::EXTERN: return parse_extern_stmt(parser);
        case TokenType::FROM:
            if (parser->next_token().type == TokenType::EXTERN)
                return parse_extern_from_import_stmt(parser);
            if (parser->next_token().type == TokenType::STRING)
                return parse_import_stmt(parser);
        case TokenType::PROPAGATE: {
            auto node = std::make_unique<PropagateStmtNode>();
            parser->consume_token();
            if (parser->current_token().type == TokenType::SEMICOLON)
                parser->consume_token();
            return node;
        }
        default: {
            auto expr = parse_expr(parser);
            if (parser->current_token().type == TokenType::SEMICOLON) {
                parser->consume_token();
            }
            return expr;
        }
    }
    return nullptr;
}
