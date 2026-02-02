#include "frontend/checker/expressions/check_primary_expr.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/checker/checker.hpp"
#include <sstream>

std::shared_ptr<nv::Type>&  check_primary_expr(nv::Checker* ch, Node* node) {
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
          // Delegar para infer_expr para evitar duplicação de lógica e erros duplicados
          // infer_expr já faz toda a verificação necessária e reporta erros corretamente
          // Como infer_expr retorna por valor e check_primary_expr retorna por referência,
          // precisamos armazenar o resultado temporariamente e retornar uma referência
          static thread_local std::shared_ptr<nv::Type> temp_result;
          temp_result = ch->infer_expr(node);
          return temp_result;
        }
        default:
          return ch->gettyptr("void");
    }
}