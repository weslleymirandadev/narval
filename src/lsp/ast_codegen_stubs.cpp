// ast_codegen_stubs.cpp — the AST nodes' nir_codegen slots, stubbed out for the LSP.
//
// Every AST node carries nir_codegen() in its vtable, and a vtable is emitted wherever the
// class' key function is defined (now the front-end: src/frontend/ast_node_anchors.cpp). A
// vtable slot is a relocation, so linking anything that deletes a node — the parser and the
// checker, which is all the language server uses — asks the linker for every nir_codegen()
// implementation, and those live in src/backend/nir/codegen/** and drag MLIR and LLVM along:
// 163 MB on Linux, against a few MB once the server stops linking the back-end.
//
// Nothing in src/lsp/ touches codegen, so these empty definitions are unreachable there and
// the server links against the front-end only. They are NOT part of the compiler: narval and
// the tests link the real ones (NarvalNirDialect). If the server ever needs codegen, it has
// to stop using this file — a stub would silently do nothing.
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
#include "frontend/ast/statements/inline_asm_stmt_node.hpp"
#include "frontend/ast/expressions/instanceof_expr_node.hpp"
#include "frontend/ast/expressions/key_value_node.hpp"
#include "frontend/ast/expressions/list_comp_node.hpp"
#include "frontend/ast/expressions/logical_not_expr_node.hpp"
#include "frontend/ast/expressions/map_node.hpp"
#include "frontend/ast/statements/match_stmt_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"
#include "frontend/ast/statements/module_attr_node.hpp"
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

void AccessExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void ArrayExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void AssignmentExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void AttributeStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void AwaitExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void BinaryExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void BooleanLiteralNode::nir_codegen(nv::NIRGenerationContext&) {}
void BreakStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void CallExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void ClassStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void ClosureExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void ConditionalExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void ContinueStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void DeclarationStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void DecoratorStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void DecrementExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void DeferErrorStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void DeferStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void EnumStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void ExternFromImportStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void ExternStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void ForStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void ForeverStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void FunctionStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void IdentifierNode::nir_codegen(nv::NIRGenerationContext&) {}
void IfStatementNode::nir_codegen(nv::NIRGenerationContext&) {}
void ImportStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void IncrementExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void InlineAsmStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void InstanceofExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void KeyValueNode::nir_codegen(nv::NIRGenerationContext&) {}
void ListCompNode::nir_codegen(nv::NIRGenerationContext&) {}
void LogicalNotExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void MapNode::nir_codegen(nv::NIRGenerationContext&) {}
void MatchStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void MemberExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void ModuleAttrNode::nir_codegen(nv::NIRGenerationContext&) {}
void NewExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void NoneLiteralNode::nir_codegen(nv::NIRGenerationContext&) {}
void NumericLiteralNode::nir_codegen(nv::NIRGenerationContext&) {}
void OrExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void PostDecrementExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void PostIncrementExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void PropagateStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void RangeExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void ReturnStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
void SelfExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void SliceExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void StringLiteralNode::nir_codegen(nv::NIRGenerationContext&) {}
void ThrowStatementNode::nir_codegen(nv::NIRGenerationContext&) {}
void TryStatementNode::nir_codegen(nv::NIRGenerationContext&) {}
void TupleExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void UnaryMinusExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void VectorExprNode::nir_codegen(nv::NIRGenerationContext&) {}
void WhileStmtNode::nir_codegen(nv::NIRGenerationContext&) {}
