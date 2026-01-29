#include "frontend/checker/checker_meth.hpp"
#include <memory>

std::shared_ptr<rph::Type>& rph::Checker::check_node(Node* node) {
  try {
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
          return gettyptr("void");
    }
  } catch (std::exception& e) {
    std::cerr << "Error at line " << node->position->line << ", " << node->position->col[0]
     << "-" << node->position->col[1] << ": "
    << e.what() << std::endl;
    err = true;
    return gettyptr("void");
  }
}