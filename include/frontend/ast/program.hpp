#pragma once
#include "types.hpp"
#include <iostream>
#include <vector>
#include <memory>
#include "expressions/identifier_node.hpp"
#include "expressions/numeric_literal_node.hpp"
#include "expressions/char_literal_node.hpp"
#include "expressions/string_literal_node.hpp"
#include "expressions/binary_expr_node.hpp"
#include "expressions/assignment_expr_node.hpp"
#include "expressions/array_expr_node.hpp"
#include "expressions/tuple_expr_node.hpp"
#include "expressions/logical_not_expr_node.hpp"
#include "expressions/unary_minus_expr_node.hpp"
#include "expressions/increment_expr_node.hpp"
#include "expressions/decrement_expr_node.hpp"
#include "expressions/post_increment_expr_node.hpp"
#include "expressions/post_decrement_expr_node.hpp"
#include "expressions/access_expr_node.hpp"
#include "expressions/member_expr_node.hpp"
#include "expressions/call_expr_node.hpp"
#include "expressions/key_value_node.hpp"
#include "expressions/map_node.hpp"
#include "expressions/list_comp_node.hpp"
#include "expressions/conditional_expr_node.hpp"
#include "expressions/vector_expr_node.hpp"
#include "expressions/boolean_literal_node.hpp"
#include "expressions/range_expr_node.hpp"
#include "expressions/new_expr_node.hpp"
#include "expressions/self_expr_node.hpp"
#include "expressions/super_expr_node.hpp"
#include "expressions/instanceof_expr_node.hpp"
#include "expressions/or_expr_node.hpp"
#include "expressions/none_literal_node.hpp"
#include "expressions/slice_expr_node.hpp"
#include "expressions/closure_expr_node.hpp"

#include "statements/return_stmt_node.hpp"
#include "statements/declaration_stmt_node.hpp"
#include "statements/function_stmt_node.hpp"
#include "statements/if_statement_node.hpp"
#include "statements/for_stmt_node.hpp"
#include "statements/forever_stmt_node.hpp"
#include "statements/while_stmt_node.hpp"
#include "statements/match_stmt_node.hpp"
#include "statements/class_stmt_node.hpp"
#include "statements/enum_stmt_node.hpp"
#include "statements/interface_stmt_node.hpp"
#include "statements/propagate_stmt_node.hpp"
#include "statements/break_stmt_node.hpp"
#include "statements/continue_stmt_node.hpp"
#include "statements/throw_stmt_node.hpp"
#include "statements/try_stmt_node.hpp"
#include "statements/import_stmt_node.hpp"

class Program : public Stmt {
public:
    CodeBlock body;

    Program() : Stmt(NodeType::Program) {}

    void add_statement(std::unique_ptr<Stmt> stmt) {
        body.push_back(std::move(stmt));
    }

    const std::vector<std::unique_ptr<Stmt>>& get_statements() const {
        return body;
    }

    Node* clone() const override {
        auto* program = new Program();
        for (const auto& stmt : body) {
            program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
        }
        if (position) {
            program->position = std::make_unique<PositionData>(*position);
        }
        return program;
    }

    void print(int indentNum = 0) const {
        print_statement(this, indentNum);
    }

    static void print_statement(const Stmt* stmt, int indentNum = 0) {
        std::string indent(indentNum * 2, ' ');
        
        if (!stmt) {
            std::cout << indent << "NULL Statement\n";
            return;
        }

        switch (stmt->kind) {
            case NodeType::Program: {
                const auto* program = static_cast<const Program*>(stmt);
                std::cout << indent << "Program:\n";
                for (const auto& stmt : program->body) {
                    print_statement(stmt.get(), indentNum + 1);
                }
                break;
            }
            case NodeType::NumericLiteral: {
                const auto* numLit = static_cast<const NumericLiteralNode*>(stmt);
                std::cout << indent << "NumericLiteral: " << numLit->value << "\n";
                break;
            }
            case NodeType::CharLiteral: {
                const auto* charLit = static_cast<const CharLiteralNode*>(stmt);
                std::cout << indent << "CharLiteral: '" << charLit->value << "'\n";
                break;
            }
            case NodeType::StringLiteral: {
                const auto* strLit = static_cast<const StringLiteralNode*>(stmt);
                std::cout << indent << "StringLiteral: \"" << strLit->value << "\"\n";
                break;
            }
            case NodeType::Identifier: {
                const auto* ident = static_cast<const IdentifierNode*>(stmt);
                std::cout << indent << "Identifier: " << ident->symbol << "\n";
                break;
            }
            case NodeType::BinaryExpression: {
                const auto* binExpr = static_cast<const BinaryExprNode*>(stmt);
                std::cout << indent << "BinaryExpression:\n";
                std::cout << indent << "  Left:\n";
                print_statement(binExpr->left.get(), indentNum + 2);
                std::cout << indent << "  Operator: " << binExpr->op << "\n";
                std::cout << indent << "  Right:\n";
                print_statement(binExpr->right.get(), indentNum + 2);
                break;
            }
            case NodeType::AssignmentExpression: {
                const auto* assignExpr = static_cast<const AssignmentExprNode*>(stmt);
                std::cout << indent << "AssignmentExpression:\n";
                std::cout << indent << "  Target:\n";
                print_statement(assignExpr->target.get(), indentNum + 2);
                std::cout << indent << "  Operator: " << assignExpr->op << std::endl;
                std::cout << indent << "  Value:\n";
                print_statement(assignExpr->value.get(), indentNum + 2);
                break;
            }
            case NodeType::ImportStatement: {
                const auto* importStmt = static_cast<const ImportStmtNode*>(stmt);
                std::cout << indent << "ImportStatement: " << importStmt->module_path;
                if (!importStmt->filename.empty()) {
                    std::cout << " (file: " << importStmt->filename << ")";
                }
                std::cout << "\n";
                if (importStmt->is_wildcard) {
                    std::cout << indent << "  Wildcard import";
                    if (!importStmt->wildcard_alias.empty()) {
                        std::cout << " as " << importStmt->wildcard_alias;
                    }
                    std::cout << "\n";
                } else {
                    std::cout << indent << "  Imports:\n";
                    for (const auto& import : importStmt->imports) {
                        std::cout << indent << "    - " << import.name;
                        if (!import.alias.empty()) {
                            std::cout << " as " << import.alias;
                        }
                        std::cout << "\n";
                    }
                }
                break;
            }
            case NodeType::ClassStatement: {
                const auto* classStmt = static_cast<const ClassStmtNode*>(stmt);
                std::cout << indent << "ClassStmt: " << classStmt->name;
                if (!classStmt->parent_class.empty()) {
                    std::cout << " extends " << classStmt->parent_class;
                }
                std::cout << "\n";
                
                if (!classStmt->fields.empty()) {
                    std::cout << indent << "  Fields:\n";
                    for (const auto& field : classStmt->fields) {
                        std::cout << indent << "    " << field->name << ": " << field->type;
                        if (field->is_mutable) {
                            std::cout << " (mut)";
                        }
                        std::cout << "\n";
                    }
                }
                
                if (!classStmt->methods.empty()) {
                    std::cout << indent << "  Methods:\n";
                    for (const auto& method : classStmt->methods) {
                        std::cout << indent << "    " << method->access_modifier << " " << method->name << "()\n";
                    }
                }
                break;
            }
            case NodeType::DeclarationStatement: {
                const auto* declStmt = static_cast<const DeclarationStmtNode*>(stmt);
                std::cout << indent << "DeclarationStatement:\n";
                std::cout << indent << "  Target:\n";
                print_statement(declStmt->target.get(), indentNum + 2);
                std::cout << indent << "  Value:";
                if (declStmt->value) {
                    std::cout << "\n";
                    print_statement(declStmt->value.get(), indentNum + 2);
                } else {
                    std::cout << " NULO\n";
                }
                std::cout << indent << "  Type: " << declStmt->typ << std::endl;
                std::cout << indent << "  Mutable: " << (declStmt->mutable_ ? "yes" : "no") << std::endl;
                break;
            }
            case NodeType::FunctionStatement: {
                const auto* functionStmt = static_cast<const FunctionStmtNode*>(stmt);
                std::cout << indent << "FunctionStmt: " << functionStmt->name << "\n";
                std::cout << indent << "  Args:\n";

                for (const auto& param : functionStmt->parameters) {
                    for (const auto& [arg_name, arg_type] : param.parameter) {
                        std::cout << indent << "    " << arg_name << ": " << arg_type << "\n";
                    }
                }

                std::cout << indent << "  Return Type: " << functionStmt->return_type << "\n";
                std::cout << indent << "  Body:\n";

                for (const auto& bodyStmt : functionStmt->body) {
                    print_statement(bodyStmt.get(), indentNum + 2);
                }

                break;
            }
            case NodeType::IfStatement: {
                const auto* ifStmt = static_cast<const IfStatementNode*>(stmt);
                std::cout << indent << "IfStatement:\n";
                std::cout << indent << "  Condition:\n";
                print_statement(ifStmt->condition.get(), indentNum + 2);
                std::cout << indent << "  Then:\n";

                for (const auto& thenStmt : ifStmt->consequent) {
                    print_statement(thenStmt.get(), indentNum + 2);
                }

                if (!ifStmt->alternate.empty()) {
                    std::cout << indent << "  Else:\n";
                    for (const auto& elseStmt : ifStmt->alternate) {
                        print_statement(elseStmt.get(), indentNum + 2);
                    }
                }

                break;
            }
            case NodeType::AccessExpression: {
                const auto* access = static_cast<const AccessExprNode*>(stmt);
                std::cout << indent << "AccessExpression:\n";
                std::cout << indent << "  Expression:\n";
                print_statement(access->expr.get(), indentNum + 2);
                std::cout << indent << "  Index:\n";
                print_statement(access->index.get(), indentNum + 2);
                break;
            }
            case NodeType::MemberExpression: {
                const auto* memberExpr = static_cast<const MemberExprNode*>(stmt);
                std::cout << indent << "MemberExpression:\n";
                std::cout << indent << "  Object:\n";
                print_statement(memberExpr->object.get(), indentNum + 2);
                std::cout << indent << "  Property:\n";
                print_statement(memberExpr->property.get(), indentNum + 2);

                break;
            }
            case NodeType::CallExpression: {
                const auto* memberExpr = static_cast<const CallExprNode*>(stmt);
                std::cout << indent << "CallExpression:\n";
                std::cout << indent << "  Caller:\n";
                print_statement(memberExpr->caller.get(), indentNum + 2);
                std::cout << indent << "  Arguments:\n";
                for (const auto& arg : memberExpr->args) {
                    print_statement(arg.get(), indentNum + 2);
                }
                break;
            }
            case NodeType::Map: {
                const auto* mapNode = static_cast<const MapNode*>(stmt);
                std::cout << indent << "Map:\n";
                for (const auto& prop : mapNode->properties) {
                    print_statement(prop.get(), indentNum + 1);
                }
                break;
            }
            case NodeType::KeyValue: {
                const auto* keyValueNode = static_cast<const KeyValueNode*>(stmt);
                std::cout << indent << "KeyValueNode:\n";
                std::cout << indent << "  Key:\n";
                print_statement(keyValueNode->key.get(), indentNum + 2);

                std::cout << indent << "  Value:\n";
                print_statement(keyValueNode->value.get(), indentNum + 2);
                break;
            }
            case NodeType::ArrayExpression: {
                const auto* arrayNode = static_cast<const ArrayExprNode*>(stmt);
                std::cout << indent << "ArrayExpression:\n";
                for (const auto& element : arrayNode->elements) {
                    print_statement(element.get(), indentNum + 1);
                }
                break;
            }
            case NodeType::VectorExpression: {
                const auto* vectorNode = static_cast<const VectorExprNode*>(stmt);
                std::cout << indent << "VectorExpression:\n";
                for (const auto& element : vectorNode->elements) {
                    print_statement(element.get(), indentNum + 1);
                }
                break;
            }
            case NodeType::TupleExpression: {
                const auto* tupleNode = static_cast<const TupleExprNode*>(stmt);
                std::cout << indent << "TupleExpression:\n";
                for (const auto& element : tupleNode->elements) {
                    print_statement(element.get(), indentNum + 1);
                }
                break;
            }
            case NodeType::ReturnStatement: {
                const auto* returnStmt = static_cast<const ReturnStmtNode*>(stmt);
                std::cout << indent << "ReturnStatement:\n";
                print_statement(returnStmt->value.get(), indentNum + 1);
                break;
            }
            case NodeType::ForStatement: {
                const auto* forStmt = static_cast<const ForStmtNode*>(stmt);
                std::cout << indent << "ForStatement:\n";
                std::cout << indent << "  Bindings:\n";
                for (const auto& binding : forStmt->bindings) {
                    print_statement(binding.get(), indentNum + 2);
                }
                if (forStmt->range_start && forStmt->range_end) {
                    std::cout << indent << "  RangeStart:\n";
                    print_statement(forStmt->range_start.get(), indentNum + 2);
                    std::cout << indent << "  RangeEnd:\n";
                    print_statement(forStmt->range_end.get(), indentNum + 2);
                }
                if (forStmt->iterable) {
                    std::cout << indent << "  Iterable:\n";
                    print_statement(forStmt->iterable.get(), indentNum + 2);
                }
                std::cout << indent << "  Body:\n";
                for (const auto& bodyStmt : forStmt->body) {
                    print_statement(bodyStmt.get(), indentNum + 2);
                }
                std::cout << indent << "  Else:\n";
                for (const auto& elseStmt : forStmt->else_block) {
                    print_statement(elseStmt.get(), indentNum + 2);
                }
                break;
            }
            case NodeType::ForeverStatement: {
                const auto* foreverStmt = static_cast<const ForeverStmtNode*>(stmt);
                std::cout << indent << "ForeverStatement:\n";
                std::cout << indent << "  Body:\n";
                
                for (const auto& bodyStmt : foreverStmt->body) {
                    print_statement(bodyStmt.get(), indentNum + 2);
                }
                
                break;
            }
            case NodeType::WhileStatement: {
                const auto* whileStmt = static_cast<const WhileStmtNode*>(stmt);
                std::cout << indent << "WhileStatement:\n";
                std::cout << indent << "  Condition:\n";
                print_statement(whileStmt->condition.get(), indentNum + 2);

                std::cout << indent << "  Body:\n";
                for (const auto& bodyStmt : whileStmt->body) {
                    print_statement(bodyStmt.get(), indentNum + 2);
                }
                
                break;
            }
            case NodeType::ConditionalExpression: {
                const auto* condExp = static_cast<const ConditionalExprNode*>(stmt);
                std::cout << indent << "ConditionalExpression:\n";
                std::cout << indent << "  TrueExpression:\n";
                print_statement(condExp->true_expr.get(), indentNum + 2);
                std::cout << indent << "  Condition:\n";
                print_statement(condExp->condition.get(), indentNum + 2);
                std::cout << indent << "  FalseExpression:\n";
                print_statement(condExp->false_expr.get(), indentNum + 2);

                break;
            }
            case NodeType::ListComprehension: {
                const auto* lc = static_cast<const ListCompNode*>(stmt);
                std::cout << indent << "ListComprehension:\n";
                std::cout << indent << "  Element:\n";
                print_statement(lc->elt.get(), indentNum + 2);

                for (const auto& [v, s] : lc->generators) {
                    if (v) {
                        if (v->kind == NodeType::Identifier) {
                            auto* id = static_cast<const IdentifierNode*>(v.get());
                            std::cout << indent << "  For " << id->symbol << " :\n";
                        } else if (v->kind == NodeType::TupleExpression) {
                            std::cout << indent << "  For (tuple):\n";
                            print_statement(v.get(), indentNum + 3);
                        } else {
                            std::cout << indent << "  For (expr):\n";
                            print_statement(v.get(), indentNum + 3);
                        }
                    } else {
                        std::cout << indent << "  For <null target> :\n";
                    }
                    print_statement(s.get(), indentNum + 3);
                }

                if (lc->if_cond) {
                    std::cout << indent << "  If:\n";
                    print_statement(lc->if_cond.get(), indentNum + 2);
                    if (lc->else_expr) {
                        std::cout << indent << "  Else:\n";
                        print_statement(lc->else_expr.get(), indentNum + 2);
                    } else {
                        std::cout << indent << "  Else: skip\n";
                    }
                }
                break;
            }
            case NodeType::MatchStatement: {
                const auto* matchStmt = static_cast<const MatchStmtNode*>(stmt);
                std::cout << indent << "MatchStatement:\n";
                std::cout << indent << "  Target:\n";
                print_statement(matchStmt->target.get(), indentNum + 2);
                std::cout << indent << "  Cases:\n";
                for (size_t i = 0; i < matchStmt->cases.size(); i++) {
                    std::cout << indent << "    If it is:\n";
                    print_statement(matchStmt->cases[i].get(), indentNum + 4);
                    std::cout << indent << "    Then run:\n";
                    for (const auto& e : matchStmt->bodies[i]) {
                        print_statement(e.get(), indentNum + 4);
                    }

                }
                break;
            }
            case NodeType::RangeExpression: {
                const auto* rangeExpr = static_cast<const RangeExprNode*>(stmt);
                std::cout << indent << "RangeExpression:\n";
                std::cout << indent << "  Start:\n";
                print_statement(rangeExpr->start.get(), indentNum + 2);
                std::cout << indent << "  End:\n";
                print_statement(rangeExpr->end.get(), indentNum + 2);
                std::cout << indent << "  Inclusive: " << (rangeExpr->inclusive ? "true" : "false") << "\n";
                break;
            }
            case NodeType::BooleanLiteral: {
                const auto* boolExpr = static_cast<const BooleanLiteralNode*>(stmt);
                std::cout << indent << "Boolean: " << (boolExpr->value ? "true" : "false") << "\n";
                break;
            }
            case NodeType::NewExpression: {
                const auto* newExpr = static_cast<const NewExprNode*>(stmt);
                std::cout << indent << "NewExpression: " << newExpr->class_name << "\n";
                std::cout << indent << "  Arguments:\n";
                for (const auto& arg : newExpr->arguments) {
                    print_statement(arg.get(), indentNum + 2);
                }
                break;
            }
            case NodeType::SelfExpression: {
                std::cout << indent << "SelfExpression\n";
                break;
            }
            case NodeType::SuperExpression: {
                std::cout << indent << "SuperExpression\n";
                break;
            }
            case NodeType::InstanceofExpression: {
                const auto* instanceofExpr = static_cast<const InstanceofExprNode*>(stmt);
                std::cout << indent << "InstanceofExpression\n";
                std::cout << indent << "  Object:\n";
                print_statement(instanceofExpr->object.get(), indentNum + 2);
                std::cout << indent << "  Class: " << instanceofExpr->class_name << "\n";
                break;
            }
            case NodeType::OrExpression: {
                const auto* orExpr = static_cast<const OrExprNode*>(stmt);
                std::cout << indent << "OrExpression\n";
                std::cout << indent << "  Expression:\n";
                print_statement(orExpr->expr.get(), indentNum + 2);
                if (orExpr->is_block_handler) {
                    std::cout << indent << "  Block Handler:\n";
                    for (const auto& stmt : orExpr->block_stmts) {
                        print_statement(stmt.get(), indentNum + 2);
                    }
                } else {
                    std::cout << indent << "  Value Handler:\n";
                    print_statement(orExpr->value_handler.get(), indentNum + 2);
                }
                break;
            }
            case NodeType::NoneLiteral: {
                std::cout << indent << "NoneLiteral\n";
                break;
            }
            case NodeType::PropagateStatement: {
                std::cout << indent << "PropagateStatement\n";
                break;
            }
            case NodeType::SliceExpression: {
                const auto* sliceExpr = static_cast<const SliceExprNode*>(stmt);
                std::cout << indent << "SliceExpression\n";
                std::cout << indent << "  Collection:\n";
                print_statement(sliceExpr->collection.get(), indentNum + 2);
                if (sliceExpr->start) {
                    std::cout << indent << "  Start:\n";
                    print_statement(sliceExpr->start.get(), indentNum + 2);
                }
                if (sliceExpr->stop) {
                    std::cout << indent << "  Stop:\n";
                    print_statement(sliceExpr->stop.get(), indentNum + 2);
                }
                if (sliceExpr->step) {
                    std::cout << indent << "  Step:\n";
                    print_statement(sliceExpr->step.get(), indentNum + 2);
                }
                break;
            }
            case NodeType::ClosureExpression: {
                const auto* closureExpr = static_cast<const ClosureExprNode*>(stmt);
                std::cout << indent << "ClosureExpression\n";
                std::cout << indent << "  Parameters:\n";
                for (const auto& param : closureExpr->parameters) {
                    std::cout << indent << "    - Name: " << param.first << ", Type: " << param.second << "\n";
                }
                std::cout << indent << "  Return Type: " << closureExpr->return_type << "\n";
                std::cout << indent << "  Body:\n";
                for (const auto& stmt : closureExpr->body) {
                    print_statement(static_cast<const Stmt*>(stmt.get()), indentNum + 2);
                }
                std::cout << indent << "  Captures:\n";
                for (const auto& capture : closureExpr->captures) {
                    std::cout << indent << "    - " << capture << "\n";
                }
                break;
            }
            case NodeType::EnumStatement: {
                const auto* enumStmt = static_cast<const EnumStmtNode*>(stmt);
                std::cout << indent << "EnumStatement\n";
                std::cout << indent << "  Name: " << enumStmt->name << "\n";
                std::cout << indent << "  Variants:\n";
                for (const auto& variant : enumStmt->variants) {
                    std::cout << indent << "    - Name: " << variant.name;
                    if (variant.has_explicit_value) {
                        std::cout << ", ExplicitValue: " << variant.explicit_value;
                    }
                    std::cout << "\n";
                }
                break;
            }
            case NodeType::InterfaceStatement: {
                const auto* interfaceStmt = static_cast<const InterfaceStmtNode*>(stmt);
                std::cout << indent << "InterfaceStatement\n";
                std::cout << indent << "  Name: " << interfaceStmt->name << "\n";
                std::cout << indent << "  Parent Interfaces:\n";
                for (const auto& parent : interfaceStmt->parent_interfaces) {
                    std::cout << indent << "    - " << parent << "\n";
                }
                std::cout << indent << "  Methods:\n";
                for (const auto& method : interfaceStmt->methods) {
                    std::cout << indent << "    - Name: " << method.name << "\n";
                    std::cout << indent << "      Parameters:\n";
                    for (const auto& param : method.params) {
                        std::cout << indent << "        - Name: " << param.first << ", Type: " << param.second << "\n";
                    }
                    std::cout << indent << "      Return Type: " << method.return_type << "\n";
                }
                break;
            }
            case NodeType::ThrowStatement: {
                const auto* throwStmt = static_cast<const ThrowStatementNode*>(stmt);
                std::cout << indent << "ThrowStatement\n";
                std::cout << indent << "  Exception:\n";
                print_statement(static_cast<const Stmt*>(throwStmt->exception.get()), indentNum + 2);
                break;
            }
            case NodeType::TryStatement: {
                const auto* tryStmt = static_cast<const TryStatementNode*>(stmt);
                std::cout << indent << "TryStatement\n";
                std::cout << indent << "  Try Body:\n";
                for (const auto& stmt : tryStmt->try_body) {
                    print_statement(static_cast<const Stmt*>(stmt.get()), indentNum + 2);
                }
                std::cout << indent << "  Catch Clauses:\n";
                for (const auto& catchClause : tryStmt->catches) {
                    std::cout << indent << "    - Exception Type: " << catchClause->exception_type << "\n";
                    std::cout << indent << "      Exception Variable: " << catchClause->exception_var << "\n";
                    std::cout << indent << "      Catch Body:\n";
                    for (const auto& stmt : catchClause->body) {
                        print_statement(static_cast<const Stmt*>(stmt.get()), indentNum + 4);
                    }
                }
                std::cout << indent << "  Finally Body:\n";
                for (const auto& stmt : tryStmt->finally_body) {
                    print_statement(static_cast<const Stmt*>(stmt.get()), indentNum + 2);
                }
                break;
            }
            default: std::cout << indent << "Unknown Statement\n";
        }
    }
};
