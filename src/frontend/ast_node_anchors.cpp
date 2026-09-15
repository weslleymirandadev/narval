// ast_node_anchors.cpp — where the AST nodes' destructors are defined.
//
// A destructor declared out-of-line is the class' key function, and the key function is
// where the Itanium ABI (GCC/Clang) emits the vtable. These destructors used to be
// declared inline in the headers, which pushed every vtable into the first virtual
// function defined out-of-line — always a nir_codegen() in src/backend/nir/codegen —
// so linking anything that deletes an AST node dragged the whole NIR/MLIR back-end in
// (163 MB on Linux). With them here, in the front-end, the server links against the
// front-end only: see src/lsp/ast_codegen_stubs.cpp for the nir_codegen slots.
//
// Adding a node: declare `~X() override;` in the header and define it below. Do not
// write `= default` in the class body — that is an inline definition again.

#include "frontend/ast/expressions/access_expr_node.hpp"
#include "frontend/ast/expressions/array_expr_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/statements/attribute_stmt_node.hpp"
#include "frontend/ast/expressions/await_expr_node.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "frontend/ast/expressions/boolean_literal_node.hpp"
#include "frontend/ast/statements/break_stmt_node.hpp"
#include "frontend/ast/expressions/call_expr_node.hpp"
#include "frontend/ast/statements/class_stmt_node.hpp"
#include "frontend/ast/expressions/closure_expr_node.hpp"
#include "frontend/ast/expressions/conditional_expr_node.hpp"
#include "frontend/ast/statements/continue_stmt_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/statements/decorator_stmt_node.hpp"
#include "frontend/ast/expressions/decrement_expr_node.hpp"
#include "frontend/ast/statements/defer_error_stmt_node.hpp"
#include "frontend/ast/statements/defer_stmt_node.hpp"
#include "frontend/ast/statements/enum_stmt_node.hpp"
#include "frontend/ast/statements/extern_from_import_stmt_node.hpp"
#include "frontend/ast/statements/extern_stmt_node.hpp"
#include "frontend/ast/statements/for_stmt_node.hpp"
#include "frontend/ast/statements/forever_stmt_node.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/statements/if_statement_node.hpp"
#include "frontend/ast/statements/import_stmt_node.hpp"
#include "frontend/ast/expressions/increment_expr_node.hpp"
#include "frontend/ast/expressions/instanceof_expr_node.hpp"
#include "frontend/ast/expressions/key_value_node.hpp"
#include "frontend/ast/expressions/list_comp_node.hpp"
#include "frontend/ast/expressions/logical_not_expr_node.hpp"
#include "frontend/ast/expressions/map_node.hpp"
#include "frontend/ast/statements/match_stmt_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"
#include "frontend/ast/expressions/new_expr_node.hpp"
#include "frontend/ast/expressions/none_literal_node.hpp"
#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include "frontend/ast/expressions/or_expr_node.hpp"
#include "frontend/ast/expressions/post_decrement_expr_node.hpp"
#include "frontend/ast/expressions/post_increment_expr_node.hpp"
#include "frontend/ast/statements/propagate_stmt_node.hpp"
#include "frontend/ast/expressions/range_expr_node.hpp"
#include "frontend/ast/statements/return_stmt_node.hpp"
#include "frontend/ast/expressions/self_expr_node.hpp"
#include "frontend/ast/expressions/slice_expr_node.hpp"
#include "frontend/ast/expressions/string_literal_node.hpp"
#include "frontend/ast/statements/throw_stmt_node.hpp"
#include "frontend/ast/statements/try_stmt_node.hpp"
#include "frontend/ast/expressions/tuple_expr_node.hpp"
#include "frontend/ast/expressions/unary_minus_expr_node.hpp"
#include "frontend/ast/expressions/vector_expr_node.hpp"
#include "frontend/ast/statements/while_stmt_node.hpp"

AccessExprNode::~AccessExprNode() = default;
ArrayExprNode::~ArrayExprNode() = default;
AssignmentExprNode::~AssignmentExprNode() = default;
AttributeStmtNode::~AttributeStmtNode() = default;
AwaitExprNode::~AwaitExprNode() = default;
BinaryExprNode::~BinaryExprNode() = default;
BooleanLiteralNode::~BooleanLiteralNode() = default;
BreakStmtNode::~BreakStmtNode() = default;
CallExprNode::~CallExprNode() = default;
ClassStmtNode::~ClassStmtNode() = default;
ClosureExprNode::~ClosureExprNode() = default;
ConditionalExprNode::~ConditionalExprNode() = default;
ContinueStmtNode::~ContinueStmtNode() = default;
DeclarationStmtNode::~DeclarationStmtNode() = default;
DecoratorStmtNode::~DecoratorStmtNode() = default;
DecrementExprNode::~DecrementExprNode() = default;
DeferErrorStmtNode::~DeferErrorStmtNode() = default;
DeferStmtNode::~DeferStmtNode() = default;
EnumStmtNode::~EnumStmtNode() = default;
ExternFromImportStmtNode::~ExternFromImportStmtNode() = default;
ExternStmtNode::~ExternStmtNode() = default;
ForStmtNode::~ForStmtNode() = default;
ForeverStmtNode::~ForeverStmtNode() = default;
FunctionStmtNode::~FunctionStmtNode() = default;
IdentifierNode::~IdentifierNode() = default;
IfStatementNode::~IfStatementNode() = default;
ImportStmtNode::~ImportStmtNode() = default;
IncrementExprNode::~IncrementExprNode() = default;
InstanceofExprNode::~InstanceofExprNode() = default;
KeyValueNode::~KeyValueNode() = default;
ListCompNode::~ListCompNode() = default;
LogicalNotExprNode::~LogicalNotExprNode() = default;
MapNode::~MapNode() = default;
MatchStmtNode::~MatchStmtNode() = default;
MemberExprNode::~MemberExprNode() = default;
NewExprNode::~NewExprNode() = default;
NoneLiteralNode::~NoneLiteralNode() = default;
NumericLiteralNode::~NumericLiteralNode() = default;
OrExprNode::~OrExprNode() = default;
PostDecrementExprNode::~PostDecrementExprNode() = default;
PostIncrementExprNode::~PostIncrementExprNode() = default;
PropagateStmtNode::~PropagateStmtNode() = default;
RangeExprNode::~RangeExprNode() = default;
ReturnStmtNode::~ReturnStmtNode() = default;
SelfExprNode::~SelfExprNode() = default;
SliceExprNode::~SliceExprNode() = default;
StringLiteralNode::~StringLiteralNode() = default;
ThrowStatementNode::~ThrowStatementNode() = default;
TryStatementNode::~TryStatementNode() = default;
TupleExprNode::~TupleExprNode() = default;
UnaryMinusExprNode::~UnaryMinusExprNode() = default;
VectorExprNode::~VectorExprNode() = default;
WhileStmtNode::~WhileStmtNode() = default;
