#include "frontend/checker/expressions/check_or_expr.hpp"
#include "frontend/ast/expressions/or_expr_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/checker/type.hpp"

namespace nv {
    // Convert undeclared assignments to declarations in or block stmts
    static void preprocess_or_block(OrExprNode* or_node, Checker* checker) {
        for (size_t i = 0; i < or_node->block_stmts.size(); i++) {
            auto& stmt = or_node->block_stmts[i];
            if (!stmt || stmt->kind != NodeType::AssignmentExpression) continue;
            auto* assign = static_cast<AssignmentExprNode*>(stmt.get());
            if (assign->target->kind != NodeType::Identifier) continue;
            auto* id = static_cast<IdentifierNode*>(assign->target.get());
            // Check if identifier already exists in scope
            try { checker->scope->get_key(id->symbol); continue; } catch (...) {}
            // Not found — convert to declaration
            auto new_target = std::unique_ptr<Expr>(static_cast<Expr*>(id->clone()));
            auto new_value  = assign->value
                ? std::unique_ptr<Expr>(static_cast<Expr*>(assign->value->clone()))
                : nullptr;
            auto decl = std::make_unique<DeclarationStmtNode>(
                std::move(new_target), std::move(new_value), "automatic", false);
            if (assign->position)
                decl->position = std::make_unique<PositionData>(*assign->position);
            stmt.reset(decl.release());
        }
    }

    std::shared_ptr<Type>& check_or_expr(Checker* checker, Node* node) {
        auto* or_node = static_cast<OrExprNode*>(node);

        // Check the base expression
        checker->check_node(or_node->expr.get());

        // Bind `err` as Error type inside the handler scope
        checker->push_scope();
        auto& error_type = checker->gettyptr("Error");
        checker->scope->put_key("err", error_type, true);

        if (or_node->is_block_handler) {
            // Convert undeclared assignments to declarations before type-checking
            preprocess_or_block(or_node, checker);
            checker->or_block_depth++;
            for (const auto& stmt : or_node->block_stmts)
                if (stmt) checker->check_node(stmt.get());
            checker->or_block_depth--;
        } else if (or_node->value_handler) {
            checker->check_node(or_node->value_handler.get());
        }

        checker->pop_scope();
        return checker->gettyptr("void");
    }
}
