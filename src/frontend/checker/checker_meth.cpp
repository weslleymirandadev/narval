#include "frontend/checker/checker_meth.hpp"
#include <memory>

std::shared_ptr<rph::Type>& rph::Checker::check_node(Node* node) {
    switch (node->kind) {
        case NodeType::NumericLiteral:
        case NodeType::StringLiteral:
        case NodeType::BooleanLiteral:
        case NodeType::Identifier:
          return check_primary_expr(this, node);
        case NodeType::Program:
          return check_program_stmt(this, node);
        case NodeType::DeclarationStatement:
          return check_decl_stmt(this, node);
        default:
          return getty("void");
    }
}