#include "frontend/checker/checker.hpp"
#include "frontend/ast/expressions/closure_expr_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include <unordered_set>

// Mirrors the logic in check_program.cpp:process_codeblock but scoped to the
static void closure_prepass(CodeBlock& body, nv::Checker& ch) {
    for (auto& stmt : body) {
        if (!stmt || stmt->kind != NodeType::AssignmentExpression) continue;
        auto* assign = static_cast<AssignmentExprNode*>(stmt.get());
        if (assign->op != "=" || !assign->target ||
            assign->target->kind != NodeType::Identifier) continue;

        auto* id = static_cast<IdentifierNode*>(assign->target.get());
        bool exists = false;
        try { ch.scope->get_key(id->symbol); exists = true; } catch (...) {}
        if (exists) continue;

        // Convert to declaration and pre-register with a fresh type variable
        auto decl = std::make_unique<DeclarationStmtNode>(
            std::unique_ptr<Expr>(static_cast<Expr*>(id->clone())),
            assign->value
                ? std::unique_ptr<Expr>(static_cast<Expr*>(assign->value->clone()))
                : nullptr,
            "automatic",
            false
        );
        if (assign->position)
            decl->position = std::make_unique<PositionData>(*assign->position);

        // Registrar como mutable (true) para que check_decl_stmt possa
        // sobrescrever com o tipo real inferido na passagem final
        ch.scope->put_key(
            id->symbol,
            std::make_shared<nv::TypeVar>(ch.unify_ctx.get_next_var_id()),
            true  // mutable = true
        );
        stmt = std::move(decl);
    }
}

namespace nv {

std::shared_ptr<Type> check_closure_expr(ClosureExprNode* node, Checker& ch) {
    std::vector<std::shared_ptr<nv::Type>> param_types;

    // Verificar se closure já foi processada (parâmetros já existem no escopo atual)
    // Isso evita processamento duplicado como foi feito para funções
    bool already_processed = false;
    if (!node->parameters.empty()) {
        try {
            // Tentar acessar o primeiro parâmetro no escopo atual
            // Se existir, a closure já foi processada
            ch.scope->get_key(node->parameters[0].first);
            already_processed = true;
        } catch (std::runtime_error&) {
            // Parâmetro não existe, closure não foi processada ainda
        }
    }
    
    // Se já foi processada, apenas criar o tipo de função sem re-verificar o corpo
    if (already_processed) {
        for (const auto& param : node->parameters) {
            std::string type_str = param.second;
            auto param_type = ch.gettyptr(type_str);
            param_types.push_back(param_type);
        }
        auto return_type = (node->return_type == "auto")
            ? ch.unify_ctx.new_type_var()
            : ch.gettyptr(node->return_type);
        return std::make_shared<Function>(param_types, return_type);
    }

    ch.push_scope();
    auto saved_return_type = ch.current_return_type;

    // Adicionar parâmetros ao escopo atual
    for (const auto& param : node->parameters) {
        // Parâmetros são obrigatoriamente tipados
        std::string type_str = param.second;
        auto param_type = ch.gettyptr(type_str);
        param_types.push_back(param_type);
        ch.scope->put_key(param.first, param_type, true);
    }
    
    // Debug removido

    // Convert undeclared assignments in body to declarations (like check_program does)
    closure_prepass(node->body, ch);

    // Permissive return type so ReturnStmtNode validation doesn't false-positive
    ch.current_return_type = ch.unify_ctx.new_type_var();

    std::shared_ptr<nv::Type> return_type = ch.gettyptr("void");
    for (const auto& stmt : node->body) {
        if (stmt) {
            auto t = ch.check_node(stmt.get());
            if (t) return_type = t;
        }
    }

    // Usar tipo de retorno especificado na closure (ou inferido se auto)
    auto closure_return_type = (node->return_type == "auto")
        ? ch.unify_ctx.new_type_var()
        : ch.gettyptr(node->return_type);
    
    // Verificar se o tipo de retorno inferido é compatível com o declarado
    if (return_type && return_type->kind != nv::Kind::VOID) {
        try {
            ch.unify_ctx.unify(return_type, closure_return_type);
        } catch (std::runtime_error& e) {
            ch.error(node, "Closure return type mismatch: " + std::string(e.what()));
        }
    }
    
    // Se o tipo de retorno é uma função (closure aninhada), aceitar como válido
    // if (closure_return_type->kind == nv::Kind::FUNCTION) {
    //     Closures que retornam closures são válidas
    //     Aceitar sem verificação de unificação
    //     return function_type;
    // }

    // Salvar referência ao escopo pai antes do pop_scope
    auto parent_scope = ch.scope;
    
    ch.pop_scope();
    ch.current_return_type = saved_return_type;

    // NÃO registrar parâmetros no escopo pai para evitar conflitos em closures aninhadas
    // Isso causa problemas com parâmetros de closures internas

    auto function_type = std::make_shared<Function>(param_types, closure_return_type);
    
    // Registrar o tipo função para que possa ser usado como parâmetro em outras closures
    std::string type_name = create_function_type_string(node->parameters, node->return_type);
    ch.scope->put_key(type_name, function_type, false);
    
    return function_type;
}
}
