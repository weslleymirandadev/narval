#include "frontend/checker/checker_meth.hpp"
#include "frontend/checker/statements/check_import_stmt.hpp"
#include "frontend/checker/statements/check_def_stmt.hpp"
#include "frontend/checker/statements/check_if_stmt.hpp"
#include "frontend/checker/statements/check_for_stmt.hpp"
#include "frontend/checker/statements/check_while_stmt.hpp"
#include "frontend/checker/statements/check_loop_stmt.hpp"
#include "frontend/checker/statements/check_return_stmt.hpp"
#include "frontend/checker/expressions/check_call_expr.hpp"
#include "frontend/checker/expressions/check_primary_expr.hpp"
#include "frontend/checker/expressions/check_unary_expr.hpp"
#include "frontend/checker/expressions/check_access_expr.hpp"
#include "frontend/checker/expressions/check_member_expr.hpp"
#include "frontend/checker/expressions/check_map_expr.hpp"
#include "frontend/checker/expressions/check_conditional_expr.hpp"
#include "frontend/checker/expressions/check_list_comp_expr.hpp"
#include "frontend/checker/expressions/check_range_expr.hpp"
#include <memory>

std::shared_ptr<nv::Type>& nv::Checker::check_node(Node* node) {
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
        case NodeType::DefStatement:
          return check_def_stmt(this, node);
        case NodeType::IfStatement:
          return check_if_stmt(this, node);
        case NodeType::ForStatement:
          return check_for_stmt(this, node);
        case NodeType::WhileStatement:
          return check_while_stmt(this, node);
        case NodeType::LoopStatement:
          return check_loop_stmt(this, node);
        case NodeType::ReturnStatement:
          return check_return_stmt(this, node);
        case NodeType::MatchStatement:
          return check_match_stmt(this, node);
        case NodeType::ImportStatement:
          return check_import_stmt(this, node);
        case NodeType::CallExpression:
          return check_call_expr(this, node);
        case NodeType::LogicalNotExpression:
        case NodeType::UnaryMinusExpression:
        case NodeType::IncrementExpression:
        case NodeType::DecrementExpression:
        case NodeType::PostIncrementExpression:
        case NodeType::PostDecrementExpression:
          return check_unary_expr(this, node);
        case NodeType::AccessExpression:
          return check_access_expr(this, node);
        case NodeType::MemberExpression:
          return check_member_expr(this, node);
        case NodeType::Map:
          return check_map_expr(this, node);
        case NodeType::ConditionalExpression:
          return check_conditional_expr(this, node);
        case NodeType::ListComprehension:
          return check_list_comp_expr(this, node);
        case NodeType::RangeExpression:
          return check_range_expr(this, node);
        case NodeType::BreakStatement:
        case NodeType::ContinueStatement:
          // break e continue não têm tipo, apenas controle de fluxo
          return gettyptr("void");
        default:
          return gettyptr("void");
    }
  } catch (std::exception& e) {
    error(node, e.what());
    return gettyptr("void");
  }
}