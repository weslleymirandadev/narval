#include "frontend/checker/statements/check_program.hpp"
#include "frontend/checker/statements/check_class_stmt.hpp"
#include "frontend/checker/statements/check_enum_stmt.hpp"
#include "frontend/checker/statements/check_interface_stmt.hpp"
#include "frontend/checker/statements/check_import_stmt.hpp"
#include "frontend/checker/statements/check_function_stmt.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/statements/if_statement_node.hpp"
#include "frontend/ast/statements/for_stmt_node.hpp"
#include "frontend/ast/statements/while_stmt_node.hpp"
#include "frontend/ast/statements/forever_stmt_node.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
#include "frontend/ast/statements/attribute_stmt_node.hpp"
#include "frontend/ast/statements/decorator_stmt_node.hpp"
#include "frontend/ast/statements/match_stmt_node.hpp"
#include "frontend/ast/expressions/or_expr_node.hpp"
#include <stdexcept>

namespace {
    bool identifier_exists(nv::Checker* checker, const std::string& symbol) {
        try {
            checker->scope->get_key(symbol);
            return true;
        } catch (std::runtime_error&) {
            return false;
        }
    }
    
    std::unique_ptr<Stmt> convert_assignment_to_declaration(
        AssignmentExprNode* assign_node,
        nv::Checker* checker
    ) {
        if (assign_node->op != "=") {
            return nullptr;
        }

        if (assign_node->target->kind != NodeType::Identifier) {
            return nullptr;
        }

        auto* id_node = static_cast<IdentifierNode*>(assign_node->target.get());
        
        if (identifier_exists(checker, id_node->symbol)) {
            return nullptr;
        }

        auto decl_node = std::make_unique<DeclarationStmtNode>(
            std::unique_ptr<Expr>(static_cast<Expr*>(id_node->clone())),
            assign_node->value 
                ? std::unique_ptr<Expr>(static_cast<Expr*>(assign_node->value->clone()))
                : nullptr,
            "automatic",
            false         // constant
        );

        if (assign_node->position) {
            decl_node->position = std::make_unique<PositionData>(*assign_node->position);
        }

        checker->scope->put_key(
            id_node->symbol,
            std::make_shared<nv::TypeVar>(checker->unify_ctx.get_next_var_id()),
            true  // mutable = true
        );

        return decl_node;
    }

    void process_nodelist(std::vector<std::unique_ptr<Node>>& nodes, nv::Checker* checker) {
        for (size_t i = 0; i < nodes.size(); i++) {
            auto& stmt = nodes[i];
            if (!stmt) continue;

            if (stmt->kind == NodeType::AssignmentExpression) {
                auto* assign_node = static_cast<AssignmentExprNode*>(stmt.get());
                auto converted = convert_assignment_to_declaration(assign_node, checker);
                if (converted) {
                    stmt.reset(converted.release());
                }
            }
            
            if (stmt->kind == NodeType::OrExpression) {
                auto* or_node = static_cast<OrExprNode*>(stmt.get());
                std::vector<std::unique_ptr<Node>> stmt_nodes;
                for (auto& stmt : or_node->block_stmts) {
                    if (stmt) {
                        stmt_nodes.push_back(std::unique_ptr<Node>(static_cast<Node*>(stmt.release())));
                    }
                }
                process_nodelist(stmt_nodes, checker);
                or_node->block_stmts.clear();
                for (auto& node : stmt_nodes) {
                    if (node) {
                        or_node->block_stmts.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(node.release())));
                    }
                }
            }
        }
    }

    void process_codeblock(CodeBlock& body, nv::Checker* checker) {
        for (size_t i = 0; i < body.size(); i++) {
            auto& stmt = body[i];
            
            if (stmt->kind == NodeType::AssignmentExpression) {
                auto* assign_node = static_cast<AssignmentExprNode*>(stmt.get());
                auto converted = convert_assignment_to_declaration(assign_node, checker);
                if (converted) {
                    stmt.reset(converted.release());
                }
            }
            
            switch (stmt->kind) {
                case NodeType::IfStatement: {
                    auto* if_stmt = static_cast<IfStatementNode*>(stmt.get());
                    process_codeblock(if_stmt->consequent, checker);
                    process_codeblock(if_stmt->alternate, checker);
                    break;
                }
                case NodeType::ForStatement: {
                    auto* for_stmt = static_cast<ForStmtNode*>(stmt.get());
                    process_codeblock(for_stmt->body, checker);
                    process_codeblock(for_stmt->else_block, checker);
                    break;
                }
                case NodeType::WhileStatement: {
                    auto* while_stmt = static_cast<WhileStmtNode*>(stmt.get());
                    process_codeblock(while_stmt->body, checker);
                    break;
                }
                case NodeType::ForeverStatement: {
                    auto* forever_stmt = static_cast<ForeverStmtNode*>(stmt.get());
                    process_codeblock(forever_stmt->body, checker);
                    break;
                }
                case NodeType::FunctionStatement: {
                    continue;
                }
                case NodeType::MatchStatement: {
                    auto* match_stmt = static_cast<MatchStmtNode*>(stmt.get());
                    for (auto& case_body : match_stmt->bodies) {
                        process_codeblock(case_body, checker);
                    }
                    break;
                }
                case NodeType::OrExpression: {
                    auto* or_node = static_cast<OrExprNode*>(stmt.get());
                    std::vector<std::unique_ptr<Node>> stmt_nodes;
                    for (auto& stmt : or_node->block_stmts) {
                        if (stmt) {
                            stmt_nodes.push_back(std::unique_ptr<Node>(static_cast<Node*>(stmt.release())));
                        }
                    }
                    process_nodelist(stmt_nodes, checker);
                    or_node->block_stmts.clear();
                    for (auto& node : stmt_nodes) {
                        if (node) {
                            or_node->block_stmts.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(node.release())));
                        }
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }

    bool no_std_allows_toplevel(NodeType kind) {
        return kind == NodeType::AttributeStatement ||
               kind == NodeType::ModuleAttrStatement ||
               kind == NodeType::DecoratorStatement ||
               kind == NodeType::FunctionStatement ||
               kind == NodeType::InlineAsmStatement ||
               kind == NodeType::ExternStatement ||
               kind == NodeType::ExternFromImportStatement;
    }

} // anonymous namespace

std::shared_ptr<nv::Type>& check_program_stmt(nv::Checker* ch, Node* node) {
    auto* program = static_cast<Program*>(node);

    if (ch->no_std_attr_node) {
        bool has_main = false;
        bool has_start = false;
        for (auto& el : program->body) {
            if (!el) continue;
            if (el->kind == NodeType::FunctionStatement) {
                auto* fn = static_cast<FunctionStmtNode*>(el.get());
                has_main = has_main || fn->name == "main";
                has_start = has_start || fn->name == "_start";
            } else if (el->kind == NodeType::ImportStatement) {
                ch->no_std_error(el.get(), "imports");
            } else if (!no_std_allows_toplevel(el->kind)) {
                ch->no_std_error(el.get(), "top-level runtime code");
            }
        }
        if (!has_main && !has_start) {
            ch->error(ch->no_std_attr_node, "@[no_std] requires a 'main' or '_start' function.");
        }
    }

    // Processes imports BEFORE EVERYTHING to track symbols on the scope
    for (auto& el : program->body) {
        if (el->kind == NodeType::ImportStatement) {
            ch->check_node(el.get());
        }
    }

    // Second pass: convert undeclared AssignmentExpression into declarations
    // Processes all code blocks recursively (including nested ones)
    process_codeblock(program->body, ch);

    // Third pass: register interfaces first (before classes).
    for (auto& el : program->body) {
        if (el->kind == NodeType::InterfaceStatement) {
            check_interface_stmt(ch, el.get());
        }
    }

    // Fourth pass: register all classes and enums.
    // This allows using classes/enums as types in function parameters/returns
    // and in declarations even when they appear later in the file.
    for (auto& el : program->body) {
        if (el->kind == NodeType::ClassStatement) {
            check_class_stmt(ch, el.get());
        } else if (el->kind == NodeType::EnumStatement) {
            check_enum_stmt(ch, el.get());
        }
    }

    // Fifth pass: register signatures of all functions (defs) before checking bodies
    for (auto& el : program->body) {
        if (el->kind == NodeType::FunctionStatement) {
            auto* function_stmt = static_cast<FunctionStmtNode*>(el.get());
            
            // Process parameters to obtain the function type
            std::vector<std::shared_ptr<nv::Type>> param_types;
            for (const auto& param : function_stmt->parameters) {
                std::string param_name;
                std::string param_type_str;
                for (const auto& [key, value] : param.parameter) {
                    param_name = key;
                    param_type_str = value;
                }
                
                std::shared_ptr<nv::Type> param_type;
                if (param_type_str.empty() || param_type_str == "automatic") {
                    param_type = std::make_shared<nv::TypeVar>(ch->unify_ctx.get_next_var_id());
                } else {
                    param_type = ch->gettyptr(param_type_str);
                }
                param_types.push_back(param_type);
            }
            
            std::shared_ptr<nv::Type> return_type;
            if (function_stmt->return_type.empty() || function_stmt->return_type == "automatic") {
                return_type = std::make_shared<nv::TypeVar>(ch->unify_ctx.get_next_var_id());
            } else {
                return_type = ch->gettyptr(function_stmt->return_type);
            }
            if (function_stmt->is_async && return_type->kind != nv::Kind::FUTURE) {
                return_type = std::make_shared<nv::Future>(return_type);
            }
            
            auto func_type = std::make_shared<nv::Function>(param_types, return_type);
            
            // Register function in scope WITHOUT checking the body yet
            // We pass false so the symbol is not locked
            ch->scope->put_key(function_stmt->name, func_type, false);
        }
    }

    // Final pass: process all remaining statements (including the converted ones)
    for (auto& el : program->body) {
        if (el->kind != NodeType::ImportStatement) {
            ch->check_node(el.get());
        }
    }

    return ch->gettyptr("void");
}
