#include "frontend/parser/statements/parse_block_util.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/ast/statements/defer_error_stmt_node.hpp"
#include "frontend/ast/statements/defer_stmt_node.hpp"

static void collect_remaining(std::vector<std::unique_ptr<Stmt>>& out, Parser* parser) {
    while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
        auto rem = parse_stmt(parser);
        if (!rem) continue;

        if (rem->kind == NodeType::DeferStatement) {
            auto* inner = static_cast<DeferStmtNode*>(rem.get());
            collect_remaining(inner->remaining_body, parser);
            out.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(rem.release())));
            return;
        }

        if (rem->kind == NodeType::DeferErrorStatement) {
            auto* inner = static_cast<DeferErrorStmtNode*>(rem.get());
            collect_remaining(inner->remaining_body, parser);
            out.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(rem.release())));
            return;
        }

        out.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(rem.release())));
    }
}

std::vector<std::unique_ptr<Stmt>> parse_body(Parser* parser) {
    std::vector<std::unique_ptr<Stmt>> stmts;

    while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
        auto node = parse_stmt(parser);
        if (!node) continue;

        if (node->kind == NodeType::DeferErrorStatement) {
            auto* defer = static_cast<DeferErrorStmtNode*>(node.get());
            collect_remaining(defer->remaining_body, parser);
            stmts.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(node.release())));
            break;
        }

        if (node->kind == NodeType::DeferStatement) {
            auto* defer = static_cast<DeferStmtNode*>(node.get());
            collect_remaining(defer->remaining_body, parser);
            stmts.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(node.release())));
            break;
        }

        stmts.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(node.release())));
    }

    return stmts;
}
