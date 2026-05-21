#include "frontend/checker/checker_meth.hpp"
#include "frontend/checker/statements/check_import_stmt.hpp"
#include "frontend/checker/statements/check_enum_stmt.hpp"
#include "frontend/checker/statements/check_interface_stmt.hpp"
#include "frontend/checker/statements/check_defer_error_stmt.hpp"
#include "frontend/checker/statements/check_defer_stmt.hpp"
#include "frontend/checker/expressions/check_await_expr.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
#include "frontend/checker/statements/check_extern_stmt.hpp"
#include "frontend/checker/expressions/check_closure_expr.hpp"
#include "frontend/checker/statements/check_extern_from_import_stmt.hpp"
#include "frontend/checker/expressions/check_or_expr.hpp"
#include "frontend/checker/statements/check_function_stmt.hpp"
#include "frontend/checker/statements/check_if_stmt.hpp"
#include "frontend/checker/statements/check_for_stmt.hpp"
#include "frontend/checker/statements/check_while_stmt.hpp"
#include "frontend/checker/statements/check_forever_stmt.hpp"
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
#include "frontend/checker/expressions/check_assignment_expr.hpp"
#include "frontend/attributes/attribute_mapper.hpp"
#include "frontend/checker/statements/check_inline_asm_stmt.hpp"
#include <memory>

std::shared_ptr<nv::Type> nv::Checker::check_node(Node* node) {
  Node* prev_node = current_node;
  if (node) current_node = node;
  struct NodeGuard { nv::Checker* ch; Node* prev; ~NodeGuard() { ch->current_node = prev; } } guard{this, prev_node};
  try {
    switch (node->kind) {
        case NodeType::NumericLiteral:
        case NodeType::StringLiteral:
        case NodeType::CharLiteral:
        case NodeType::BooleanLiteral:
        case NodeType::Identifier:
          return check_primary_expr(this, node);
        case NodeType::Program:
          apply_compilation_attributes(nv::map_compilation_attributes(node));
          return check_program_stmt(this, node);
        case NodeType::DeclarationStatement:
          return check_decl_stmt(this, node);
        case NodeType::FunctionStatement: {
          auto* fn = static_cast<FunctionStmtNode*>(node);
          if (fn->is_async && no_std_attr_node) { no_std_error(node, "async def"); return gettyptr("None"); }
          return check_function_stmt(this, node);
        }
        case NodeType::IfStatement:
          return check_if_stmt(this, node);
        case NodeType::ForStatement:
          return check_for_stmt(this, node);
        case NodeType::WhileStatement:
          return check_while_stmt(this, node);
        case NodeType::ForeverStatement:
          return check_forever_stmt(this, node);
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
        case NodeType::SliceExpression:
          return gettyptr("vector");
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
        case NodeType::NewExpression:
          return check_primary_expr(this, node);
        case NodeType::ClassStatement:
          return gettyptr("None");
        case NodeType::EnumStatement:
          return check_enum_stmt(this, node);
        case NodeType::InterfaceStatement:
          return check_interface_stmt(this, node);
        case NodeType::AssignmentExpression:
          return check_assignment_expr(this, node);
        case NodeType::OrExpression:
          return check_or_expr(this, node);
        case NodeType::NoneLiteral:
          return gettyptr("None");
        case NodeType::PropagateStatement:
          return gettyptr("None");
        case NodeType::BreakStatement:
        case NodeType::ContinueStatement:
          return gettyptr("None");
        case NodeType::TryStatement:
          if (no_std_attr_node) no_std_error(node, "try/catch");
          return gettyptr("None");
        case NodeType::ThrowStatement:
          if (no_std_attr_node) no_std_error(node, "throw");
          return gettyptr("None");
        case NodeType::DeferErrorStatement:
          if (no_std_attr_node) { no_std_error(node, "defer error"); return gettyptr("None"); }
          return check_defer_error_stmt(this, node);
        case NodeType::DeferStatement:
          if (no_std_attr_node) { no_std_error(node, "defer"); return gettyptr("None"); }
          return check_defer_stmt(this, node);
        case NodeType::ExternStatement:
          return check_extern_stmt(this, node);
        case NodeType::ExternFromImportStatement:
          return check_extern_from_import_stmt(this, node);
        case NodeType::ModuleAttrStatement:
          return gettyptr("None"); // atributos de módulo: sem checagem de tipo
        case NodeType::AttributeStatement:
          return gettyptr("None"); // atributos: sem checagem de tipo
        case NodeType::DecoratorStatement:
          return gettyptr("None"); // decorators: sem checagem de tipo
        case NodeType::InlineAsmStatement:
          return check_inline_asm_stmt(this, node);
        case NodeType::AwaitExpression:
          return check_await_expr(this, node);
        case NodeType::ClosureExpression: {
          auto* closure = static_cast<ClosureExprNode*>(node);
          return check_closure_expr(closure, *this); // closures: tipo função específico
        }
        default:
          return gettyptr("None");
    }
  } catch (std::exception& e) {
    error(node, e.what());
    return gettyptr("None");
  }
}
