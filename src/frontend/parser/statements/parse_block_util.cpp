#include "frontend/parser/statements/parse_block_util.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/ast/statements/defer_error_stmt_node.hpp"

std::vector<std::unique_ptr<Stmt>> parse_body(Parser* parser) {
    std::vector<std::unique_ptr<Stmt>> stmts;

    while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
        auto node = parse_stmt(parser);
        if (!node) continue;

        if (node->kind == NodeType::DeferErrorStatement) {
            auto* defer = static_cast<DeferErrorStmtNode*>(node.get());
            // Coletar todos os statements restantes no bloco como o corpo protegido
            while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
                auto rem = parse_stmt(parser);
                if (rem) {
                    defer->remaining_body.push_back(
                        std::unique_ptr<Stmt>(static_cast<Stmt*>(rem.release())));
                }
            }
            stmts.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(node.release())));
            break; // defer consome o resto do bloco
        }

        stmts.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(node.release())));
    }

    return stmts;
}
