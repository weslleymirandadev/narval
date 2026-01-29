#include "frontend/checker/expressions/check_primary_expr.hpp"
#include "frontend/ast/ast.hpp"

std::shared_ptr<rph::Type>&  check_primary_expr(rph::Checker* ch, Node* node) {
  std::cout << "passara\n";
    switch (node->kind) {
        case NodeType::NumericLiteral: {
          const auto* vl = static_cast<NumericLiteralNode*>(node);
          if (vl->value.find('.') != std::string::npos) {
            return ch->gettyptr("float");
          }
          return ch->gettyptr("int");
        }
        case NodeType::StringLiteral:
          return ch->gettyptr("string");
        case NodeType::BooleanLiteral:
          return ch->gettyptr("bool");
        case NodeType::Identifier: {
          const auto* vl = static_cast<IdentifierNode*>(node);
          return ch->scope->get_key(vl->symbol);
        }
        default:
          return ch->gettyptr("void");
    }
}