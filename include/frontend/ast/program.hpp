#pragma once
#include "types.hpp"
#include <iostream>
#include <vector>
#include <memory>
#include "expressions/identifier_node.hpp"
#include "expressions/numeric_literal_node.hpp"
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
#include "statements/return_stmt_node.hpp"
#include "statements/declaration_stmt_node.hpp"
#include "statements/def_stmt_node.hpp"
#include "statements/if_statement_node.hpp"
#include "statements/for_stmt_node.hpp"
#include "statements/loop_stmt_node.hpp"
#include "statements/while_stmt_node.hpp"
#include "statements/match_stmt_node.hpp"

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
        auto new_program = std::make_unique<Program>();
        for (const auto& stmt : body) {
            new_program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
        }
        return new_program.release();
    }

    // Debug
    void print() {
        std::cout << "Program: \n";
        for (const auto& stmt : body) {
            print_statement(stmt.get(), 1);
        }
    }

private:
    static void print_statement(const Stmt* stmt, int indentNum) {
        std::string indent(indentNum * 2, ' ');

        switch (stmt->kind) {
            case NodeType::NumericLiteral: {
                const auto* numLit = static_cast<const NumericLiteralNode*>(stmt);
                std::cout << indent << "NumericLiteral: " << numLit->value << "\n";
                break;
            }
            case NodeType::Identifier: {
                const auto* ident = static_cast<const IdentifierNode*>(stmt);
                std::cout << indent << "Identifier: " << ident->symbol << "\n";
                break;
            }
            case NodeType::StringLiteral: {
                const auto* strLit = static_cast<const StringLiteralNode*>(stmt);
                std::cout << indent << "StringLiteral: " << strLit->value << "\n";
                break;
            }
            case NodeType::LogicalNotExpression: {
                const auto* not_expr = static_cast<const LogicalNotExprNode*>(stmt);
                std::cout << indent << "LogicalNotExpression:\n";
                print_statement(not_expr->operand.get(), indentNum + 1);
                break;
            }
            case NodeType::UnaryMinusExpression: {
                const auto* not_expr = static_cast<const UnaryMinusExprNode*>(stmt);
                std::cout << indent << "UnaryMinusExpression:\n";
                print_statement(not_expr->operand.get(), indentNum + 1);
                break;
            }
            case NodeType::IncrementExpression: {
                const auto* not_expr = static_cast<const IncrementExprNode*>(stmt);
                std::cout << indent << "IncrementExpression:\n";
                print_statement(not_expr->operand.get(), indentNum + 1);
                break;
            }
            case NodeType::DecrementExpression: {
                const auto* not_expr = static_cast<const DecrementExprNode*>(stmt);
                std::cout << indent << "IncrementExpression:\n";
                print_statement(not_expr->operand.get(), indentNum + 1);
                break;
            }
            case NodeType::PostIncrementExpression: {
                const auto* not_expr = static_cast<const PostIncrementExprNode*>(stmt);
                std::cout << indent << "PostIncrementExpression:\n";
                print_statement(not_expr->operand.get(), indentNum + 1);
                break;
            }
            case NodeType::PostDecrementExpression: {
                const auto* not_expr = static_cast<const PostDecrementExprNode*>(stmt);
                std::cout << indent << "PostIncrementExpression:\n";
                print_statement(not_expr->operand.get(), indentNum + 1);
                break;
            }
            case NodeType::BinaryExpression: {
                const auto* binExpr = static_cast<const BinaryExprNode*>(stmt);
                std::cout << indent << "BinaryExpression: " << binExpr->op << "\n";
                print_statement(binExpr->left.get(), indentNum + 1);
                print_statement(binExpr->right.get(), indentNum + 1);
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
                std::cout << indent << "  Locked: " << (declStmt->constant ? "yes" : "no") << std::endl;
                break;
            }
            case NodeType::DefStatement: {
                const auto* defStmt = static_cast<const DefStmtNode*>(stmt);
                std::cout << indent << "DefStatement: " << defStmt->name << "\n";
                std::cout << indent << "  Args:\n";

                for (const auto& param : defStmt->parameters) {
                    for (const auto& [arg_name, arg_type] : param.parameter) {
                        std::cout << indent << "    " << arg_name << ": " << arg_type << "\n";
                    }
                }

                std::cout << indent << "  Return Type: " << defStmt->return_type << "\n";
                std::cout << indent << "  Body:\n";

                for (const auto& bodyStmt : defStmt->body) {
                    print_statement(bodyStmt.get(), indentNum + 2);
                }

                break;
            }
            case NodeType::IfStatement: {
                const auto* ifStmt = static_cast<const IfStatementNode*>(stmt);
                std::cout << indent << "IfStatement:\n";
                std::cout << indent << "  Condition:\n";
                print_statement(ifStmt->condition.get(), indentNum + 2);
                std::cout << indent << "  Consequent:\n";
                for (const auto& consStmt : ifStmt->consequent) {
                    print_statement(consStmt.get(), indentNum + 2);
                }
                std::cout << indent << "  Alternate:\n";
                for (const auto& altStmt : ifStmt->alternate) {
                    print_statement(altStmt.get(), indentNum + 2);
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
            case NodeType::LoopStatement: {
                const auto* loopStmt = static_cast<const LoopStmtNode*>(stmt);
                std::cout << indent << "LoopStatement:\n";
                std::cout << indent << "  Body:\n";
                
                for (const auto& bodyStmt : loopStmt->body) {
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
                        // Dependendo do tipo de v, pode ser Identifier ou TupleExprNode, etc.
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
            default: std::cout << indent << "Unknown Statement\n";
        }
    }
};

