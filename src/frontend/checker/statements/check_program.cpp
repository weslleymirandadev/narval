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
#include "frontend/comptime/comptime_evaluator.hpp"
#include "frontend/comptime/derive_generator.hpp"
#include "frontend/comptime/autodiff.hpp"
#include <stdexcept>
#include <unordered_set>

namespace {
    // Why a `@[vectorize]` loop cannot be vectorized, or "" when it can. The rule is the
    // one the backend can honour: a range loop whose iterations are independent. The
    // loop-carried test mirrors the loop codegen — a name written in the body and never
    // declared there is an outer variable, i.e. state carried between iterations — and
    // I/O is refused because a runtime call per element is exactly what keeps the
    // backend from producing SIMD.
    std::string loop_vectorize_refusal(const ForStmtNode* loop) {
        if (!loop->range_start || !loop->range_end)
            return "@[vectorize] needs a range loop (for i in a..b): iterating a "
                   "collection reaches the elements through the runtime, one call at a "
                   "time";

        std::unordered_set<std::string> declared;
        for (const auto& stmt : loop->body) {
            if (!stmt) continue;
            if (stmt->kind == NodeType::DeclarationStatement) {
                auto* decl = static_cast<DeclarationStmtNode*>(stmt.get());
                if (decl->target && decl->target->kind == NodeType::Identifier)
                    declared.insert(static_cast<IdentifierNode*>(decl->target.get())->symbol);
            }
        }

        for (const auto& stmt : loop->body) {
            if (!stmt) continue;
            if (stmt->kind == NodeType::AssignmentExpression) {
                auto* asg = static_cast<AssignmentExprNode*>(stmt.get());
                if (asg->target && asg->target->kind == NodeType::Identifier) {
                    const std::string& name =
                        static_cast<IdentifierNode*>(asg->target.get())->symbol;
                    if (!declared.count(name))
                        return "'" + name + "' is written in the loop but declared outside "
                               "it: a loop-carried dependence, so the iterations are not "
                               "independent";
                }
            } else if (stmt->kind == NodeType::CallExpression) {
                auto* call = static_cast<CallExprNode*>(stmt.get());
                if (call->caller && call->caller->kind == NodeType::Identifier) {
                    const std::string& name =
                        static_cast<IdentifierNode*>(call->caller.get())->symbol;
                    if (name == "write" || name == "read" || name == "print")
                        return "the body does I/O ('" + name + "'), which cannot happen "
                               "once per SIMD lane";
                }
            }
        }
        return "";
    }

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

    // Registers the signature of every top-level `def` in scope, never the body, so
    // that statements after it can call it. Called twice: before the comptime
    // expansion (type reflection needs the names already registered) and again
    // afterwards, because `@emit` can inject new defs — an unregistered generated
    // function is "Identifier not found" at every call site.
    void register_function_signatures(nv::Checker* ch, CodeBlock& body) {
        for (auto& el : body) {
            if (el->kind != NodeType::FunctionStatement) continue;
            auto* function_stmt = static_cast<FunctionStmtNode*>(el.get());
            Node* prev_cn = ch->current_node;
            ch->current_node = el.get();

            // Registrar type params genéricos como TypeVars para resolver a assinatura.
            // Salvar e restaurar ao fim para evitar vazamento para o escopo global.
            std::vector<int> tp_ids;
            std::vector<std::pair<std::string, std::shared_ptr<nv::Type>>> saved_tp;
            for (const auto& tp_name : function_stmt->type_params) {
                auto prev_it = ch->types.find(tp_name);
                saved_tp.push_back({tp_name,
                    prev_it != ch->types.end() ? prev_it->second : nullptr});
                auto tv = ch->unify_ctx.new_type_var();
                ch->types[tp_name] = tv;
                tp_ids.push_back(tv->id);
            }

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
                    param_type = ch->unify_ctx.new_type_var();
                } else {
                    param_type = ch->gettyptr(param_type_str, el.get());
                }
                param_types.push_back(param_type);
            }

            std::shared_ptr<nv::Type> return_type;
            if (function_stmt->return_type.empty() || function_stmt->return_type == "automatic") {
                return_type = ch->unify_ctx.new_type_var();
            } else {
                return_type = ch->gettyptr(function_stmt->return_type, el.get());
            }
            if (function_stmt->is_async && return_type->kind != nv::Kind::FUTURE) {
                return_type = std::make_shared<nv::Future>(return_type);
            }

            std::shared_ptr<nv::Type> func_type = std::make_shared<nv::Function>(param_types, return_type);

            // Para funções genéricas: generalizar em PolyType com os type params declarados
            if (!tp_ids.empty()) {
                std::unordered_set<int> bound(tp_ids.begin(), tp_ids.end());
                func_type = std::make_shared<nv::PolyType>(bound, func_type);
            }

            // Register function in scope WITHOUT checking the body yet
            ch->scope->put_key(function_stmt->name, func_type, false);

            // Restaurar type params (remover os que não existiam antes)
            for (const auto& [name, prev] : saved_tp) {
                if (prev) ch->types[name] = prev;
                else      ch->types.erase(name);
            }
            ch->current_node = prev_cn;
        }
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

    // @[derive(...)] / @[diff(var, "dfdx")] — compile-time code generation.
    // Must run BEFORE classes/functions are registered and before codegen.
    struct PendingInsert { size_t index; std::unique_ptr<Node> node; };
    std::vector<PendingInsert> pending_inserts;
    // User derives are `comptime def derive_<name>` in this module. Registering them
    // happens in the expansion pass below, which runs after this one, so the evaluator
    // used to call them is seeded here.
    nv::ComptimeEvaluator derive_ct(ch);
    derive_ct.register_user_derives(program->body);
    for (size_t i = 0; i < program->body.size(); ++i) {
        auto& el = program->body[i];
        if (!el || el->kind != NodeType::AttributeStatement) continue;
        auto* attr = static_cast<AttributeStmtNode*>(el.get());

        // The annotated statement is the next non-null one.
        Stmt* target = nullptr;
        for (size_t j = i + 1; j < program->body.size(); ++j) {
            if (program->body[j]) { target = program->body[j].get(); break; }
        }
        if (!target) continue;

        if (attr->has_attr("derive")) {
            std::vector<std::string> derives;
            for (auto& entry : attr->entries)
                if (entry.name == "derive")
                    for (auto& arg : entry.args) derives.push_back(arg.value);
            if (target->kind != NodeType::ClassStatement) {
                ch->comptime_error(el.get(), "CE001", "invalid @derive target",
                                   { "@[derive(...)] must annotate a class" });
                continue;
            }
            std::string err;
            if (!nv::apply_derive(ch, static_cast<ClassStmtNode*>(target), derives, err, &derive_ct)) {
                ch->comptime_error(el.get(), "CE001", "compile-time code generation failed", { err });
                return ch->gettyptr("None");
            }
            continue;
        }

        if (attr->has_attr("vectorize")) {
            // `@[vectorize]` annotates a loop: the codegen tags the loop and names its
            // body block, and the tensor work in the body is the part that becomes SIMD
            // (NarvalLinalgVectorizePass). A body whose iterations are not independent is
            // refused here instead of promising SIMD the backend cannot deliver.
            if (target->kind != NodeType::ForStatement) {
                ch->comptime_error(el.get(), "CE001", "invalid @vectorize target",
                                   { "@[vectorize] must annotate a for loop" });
                continue;
            }
            const std::string why =
                loop_vectorize_refusal(static_cast<ForStmtNode*>(target));
            if (!why.empty()) {
                ch->comptime_error(el.get(), "CE001", "loop is not vectorizable", { why });
                continue;
            }
            continue;
        }

        if (attr->has_attr("diff")) {
            // @[diff(x, "dfdx")] on a function: build the symbolic derivative
            // here and inject it right after, so it is checked and compiled
            // like any other function.
            std::vector<std::string> dargs;
            for (auto& entry : attr->entries)
                if (entry.name == "diff")
                    for (auto& a : entry.args) dargs.push_back(a.value);
            if (dargs.size() != 2) {
                ch->comptime_error(el.get(), "CE001", "invalid @diff attribute",
                                   { "@[diff(var, \"dfdx\")] expects exactly two arguments" });
                continue;
            }
            if (target->kind != NodeType::FunctionStatement) {
                ch->comptime_error(el.get(), "CE001", "invalid @diff target",
                                   { "@[diff(...)] must annotate a function" });
                continue;
            }
            std::string err;
            auto derived = nv::make_derivative(static_cast<FunctionStmtNode*>(target),
                                               dargs[0], dargs[1], err);
            if (!derived) {
                ch->comptime_error(el.get(), "CE001", "compile-time differentiation failed",
                                   { err });
                return ch->gettyptr("None");
            }
            pending_inserts.push_back({i + 1, std::move(derived)});
        }
    }

    for (size_t k = pending_inserts.size(); k-- > 0;) {
        std::unique_ptr<Stmt> stmt(static_cast<Stmt*>(pending_inserts[k].node.release()));
        program->body.insert(program->body.begin() + pending_inserts[k].index, std::move(stmt));
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
    register_function_signatures(ch, program->body);

    // Comptime expansion (CTE): fold every `comptime` construct into plain AST
    // before the bodies are type checked. Class/enum/function names are already
    // registered at this point, so type reflection can resolve them.
    {
        nv::ComptimeEvaluator ct(ch);
        // Declarations first, so a macro is usable regardless of where its `comptime def`
        // sits — a stdlib module merged in can land after the call site.
        ct.register_macros(program->body);
        program->body = ct.expand_body(std::move(program->body));
        if (ct.failed()) {
            const std::string code = ct.error_code();
            const std::string title = (code == "CE003")
                ? "non-comptime value used in comptime context"
                : "compile-time evaluation failed";
            if (const PositionData* pos = ct.error_position())
                ch->comptime_error_at(pos, code, title, { ct.error_message() });
            else
                ch->comptime_error(program, code, title, { ct.error_message() });
            return ch->gettyptr("None");
        }
    }

    // The expansion can inject new defs (`@emit`); register their signatures too,
    // or every call to a generated function fails with "Identifier not found".
    register_function_signatures(ch, program->body);

    // Final pass: process all remaining statements (including the converted ones)
    for (auto& el : program->body) {
        if (el->kind != NodeType::ImportStatement) {
            ch->check_node(el.get());
        }
    }

    return ch->gettyptr("None");
}
