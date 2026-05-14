#include "frontend/ast/expressions/access_expr_node.hpp"
#include "frontend/ast/expressions/array_expr_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "frontend/ast/expressions/boolean_literal_node.hpp"
#include "frontend/ast/expressions/call_expr_node.hpp"
#include "frontend/ast/expressions/char_literal_node.hpp"
#include "frontend/ast/expressions/closure_expr_node.hpp"
#include "frontend/ast/expressions/conditional_expr_node.hpp"
#include "frontend/ast/expressions/decrement_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/increment_expr_node.hpp"
#include "frontend/ast/expressions/key_value_node.hpp"
#include "frontend/ast/expressions/list_comp_node.hpp"
#include "frontend/ast/expressions/logical_not_expr_node.hpp"
#include "frontend/ast/expressions/map_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"
#include "frontend/ast/expressions/new_expr_node.hpp"
#include "frontend/ast/expressions/none_literal_node.hpp"
#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include "frontend/ast/expressions/or_expr_node.hpp"
#include "frontend/ast/expressions/param_node.hpp"
#include "frontend/ast/expressions/post_decrement_expr_node.hpp"
#include "frontend/ast/expressions/post_increment_expr_node.hpp"
#include "frontend/ast/expressions/range_expr_node.hpp"
#include "frontend/ast/expressions/self_expr_node.hpp"
#include "frontend/ast/expressions/slice_expr_node.hpp"
#include "frontend/ast/expressions/string_literal_node.hpp"
#include "frontend/ast/expressions/tuple_expr_node.hpp"
#include "frontend/ast/expressions/unary_minus_expr_node.hpp"
#include "frontend/ast/expressions/vector_expr_node.hpp"
#include "frontend/ast/statements/attribute_stmt_node.hpp"
#include "frontend/ast/statements/break_stmt_node.hpp"
#include "frontend/ast/statements/class_stmt_node.hpp"
#include "frontend/ast/statements/continue_stmt_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/statements/decorator_stmt_node.hpp"
#include "frontend/ast/statements/defer_error_stmt_node.hpp"
#include "frontend/ast/statements/enum_stmt_node.hpp"
#include "frontend/ast/statements/extern_from_import_stmt_node.hpp"
#include "frontend/ast/statements/extern_stmt_node.hpp"
#include "frontend/ast/statements/for_stmt_node.hpp"
#include "frontend/ast/statements/forever_stmt_node.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
#include "frontend/ast/statements/if_statement_node.hpp"
#include "frontend/ast/statements/import_stmt_node.hpp"
#include "frontend/ast/statements/match_stmt_node.hpp"
#include "frontend/ast/statements/module_attr_node.hpp"
#include "frontend/ast/statements/propagate_stmt_node.hpp"
#include "frontend/ast/statements/return_stmt_node.hpp"
#include "frontend/ast/statements/throw_stmt_node.hpp"
#include "frontend/ast/statements/try_stmt_node.hpp"
#include "frontend/ast/statements/while_stmt_node.hpp"

#define FRONTEND_ONLY_CODEGEN_STUB(NodeClass) \
    void NodeClass::codegen(nv::IRGenerationContext&) {}

FRONTEND_ONLY_CODEGEN_STUB(AccessExprNode)
FRONTEND_ONLY_CODEGEN_STUB(ArrayExprNode)
FRONTEND_ONLY_CODEGEN_STUB(AssignmentExprNode)
FRONTEND_ONLY_CODEGEN_STUB(AttributeStmtNode)
FRONTEND_ONLY_CODEGEN_STUB(BinaryExprNode)
FRONTEND_ONLY_CODEGEN_STUB(BooleanLiteralNode)
FRONTEND_ONLY_CODEGEN_STUB(BreakStmtNode)
FRONTEND_ONLY_CODEGEN_STUB(CallExprNode)
FRONTEND_ONLY_CODEGEN_STUB(CharLiteralNode)
FRONTEND_ONLY_CODEGEN_STUB(ClassStmtNode)
FRONTEND_ONLY_CODEGEN_STUB(ClosureExprNode)
FRONTEND_ONLY_CODEGEN_STUB(ConditionalExprNode)
FRONTEND_ONLY_CODEGEN_STUB(ContinueStmtNode)
FRONTEND_ONLY_CODEGEN_STUB(DeclarationStmtNode)
FRONTEND_ONLY_CODEGEN_STUB(DecoratorStmtNode)
FRONTEND_ONLY_CODEGEN_STUB(DecrementExprNode)
FRONTEND_ONLY_CODEGEN_STUB(DeferErrorStmtNode)
FRONTEND_ONLY_CODEGEN_STUB(EnumStmtNode)
FRONTEND_ONLY_CODEGEN_STUB(ExternFromImportStmtNode)
FRONTEND_ONLY_CODEGEN_STUB(ExternStmtNode)
FRONTEND_ONLY_CODEGEN_STUB(ForStmtNode)
FRONTEND_ONLY_CODEGEN_STUB(ForeverStmtNode)
FRONTEND_ONLY_CODEGEN_STUB(FunctionStmtNode)
FRONTEND_ONLY_CODEGEN_STUB(IdentifierNode)
FRONTEND_ONLY_CODEGEN_STUB(IfStatementNode)
FRONTEND_ONLY_CODEGEN_STUB(ImportStmtNode)
FRONTEND_ONLY_CODEGEN_STUB(IncrementExprNode)
FRONTEND_ONLY_CODEGEN_STUB(KeyValueNode)
FRONTEND_ONLY_CODEGEN_STUB(ListCompNode)
FRONTEND_ONLY_CODEGEN_STUB(LogicalNotExprNode)
FRONTEND_ONLY_CODEGEN_STUB(MapNode)
FRONTEND_ONLY_CODEGEN_STUB(MatchStmtNode)
FRONTEND_ONLY_CODEGEN_STUB(MemberExprNode)
FRONTEND_ONLY_CODEGEN_STUB(ModuleAttrNode)
FRONTEND_ONLY_CODEGEN_STUB(NewExprNode)
FRONTEND_ONLY_CODEGEN_STUB(NoneLiteralNode)
FRONTEND_ONLY_CODEGEN_STUB(NumericLiteralNode)
FRONTEND_ONLY_CODEGEN_STUB(OrExprNode)
FRONTEND_ONLY_CODEGEN_STUB(ParamNode)
FRONTEND_ONLY_CODEGEN_STUB(PostDecrementExprNode)
FRONTEND_ONLY_CODEGEN_STUB(PostIncrementExprNode)
FRONTEND_ONLY_CODEGEN_STUB(PropagateStmtNode)
FRONTEND_ONLY_CODEGEN_STUB(RangeExprNode)
FRONTEND_ONLY_CODEGEN_STUB(ReturnStmtNode)
FRONTEND_ONLY_CODEGEN_STUB(SelfExprNode)
FRONTEND_ONLY_CODEGEN_STUB(SliceExprNode)
FRONTEND_ONLY_CODEGEN_STUB(StringLiteralNode)
FRONTEND_ONLY_CODEGEN_STUB(ThrowStatementNode)
FRONTEND_ONLY_CODEGEN_STUB(TryStatementNode)
FRONTEND_ONLY_CODEGEN_STUB(TupleExprNode)
FRONTEND_ONLY_CODEGEN_STUB(UnaryMinusExprNode)
FRONTEND_ONLY_CODEGEN_STUB(VectorExprNode)
FRONTEND_ONLY_CODEGEN_STUB(WhileStmtNode)

#undef FRONTEND_ONLY_CODEGEN_STUB

IdentifierNode::~IdentifierNode() = default;
