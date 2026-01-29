#include "frontend/checker/checker.hpp"
#include "frontend/checker/type.hpp"
#include "frontend/checker/unification.hpp"
#include "frontend/checker/builtins.hpp"
#include "frontend/ast/ast.hpp"
#include <memory>
#include <unordered_set>
#include <algorithm>

rph::Checker::Checker() {
    auto globalnamespace = std::make_shared<Namespace>();
    namespaces.push_back(globalnamespace);
    scope = globalnamespace;
    types["int"] = std::make_shared<rph::Int>();
    types["string"] = std::make_shared<rph::String>();
    types["float"] = std::make_shared<rph::Float>();
    types["bool"] = std::make_shared<rph::Boolean>();
    types["void"] = std::make_shared<rph::Void>();
    
    // Registrar funções builtin do runtime
    register_builtins(*this);
}

rph::Type& rph::Checker::getty(std::string ty) {
    return *types.at(ty);
}

std::shared_ptr<rph::Type>& rph::Checker::gettyptr(std::string ty){
    return types.at(ty);
}
void rph::Checker::push_scope() {
    auto ns = std::make_shared<Namespace>(scope);
    namespaces.push_back(ns);
    scope = ns;
}

void rph::Checker::pop_scope() {
    namespaces.pop_back();
    scope = namespaces[namespaces.size() - 1];
}

std::unordered_set<int> rph::Checker::get_free_vars_in_env() {
    std::unordered_set<int> free_vars;
    
    // Coletar variáveis livres de todas as variáveis no ambiente atual
    // O método collect_free_vars do Namespace já coleta recursivamente
    // de todos os escopos pais através da cadeia de parent, então
    // precisamos apenas chamar no escopo atual
    if (scope) {
        scope->collect_free_vars(free_vars);
    }
    
    return free_vars;
}

std::shared_ptr<rph::Type> rph::Checker::infer_type(Node* node) {
    return infer_expr(node);
}

std::shared_ptr<rph::Type> rph::Checker::infer_expr(Node* node) {
    switch (node->kind) {
        case NodeType::NumericLiteral: {
            const auto* vl = static_cast<NumericLiteralNode*>(node);
            if (vl->value.find('.') != std::string::npos) {
                return gettyptr("float");
            }
            return gettyptr("int");
        }
        case NodeType::StringLiteral:
            return gettyptr("string");
        case NodeType::BooleanLiteral:
            return gettyptr("bool");
        case NodeType::Identifier: {
            const auto* id = static_cast<IdentifierNode*>(node);
            auto& var_type = scope->get_key(id->symbol);
            // Se for tipo polimórfico, instanciar
            if (var_type->kind == Kind::POLY_TYPE) {
                auto poly = std::static_pointer_cast<PolyType>(var_type);
                int next_id = unify_ctx.get_next_var_id();
                return poly->instantiate(next_id);
            }
            return var_type;
        }
        case NodeType::BinaryExpression: {
            const auto* bin = static_cast<BinaryExprNode*>(node);
            auto left_type = infer_expr(bin->left.get());
            auto right_type = infer_expr(bin->right.get());
            
            // Resolver tipos antes de unificar
            left_type = unify_ctx.resolve(left_type);
            right_type = unify_ctx.resolve(right_type);
            
            // Verificar coerção implícita int -> float antes de unificar
            bool left_is_int = left_type->kind == Kind::INT;
            bool left_is_float = left_type->kind == Kind::FLOAT;
            bool right_is_int = right_type->kind == Kind::INT;
            bool right_is_float = right_type->kind == Kind::FLOAT;
            
            // Se um é int e outro é float, promover int para float
            if (left_is_int && right_is_float) {
                left_type = gettyptr("float");
            } else if (left_is_float && right_is_int) {
                right_type = gettyptr("float");
            }
            
            // Unificar tipos dos operandos
            try {
                unify_ctx.unify(left_type, right_type);
            } catch (std::runtime_error& e) {
                throw std::runtime_error("Binary expression type error: " + std::string(e.what()));
            }
            
            // Resolver tipos após unificação
            left_type = unify_ctx.resolve(left_type);
            
            // Determinar tipo de retorno baseado no operador
            if (bin->op == "+" || bin->op == "-" || bin->op == "*" || bin->op == "/" || bin->op == "%") {
                // Operadores aritméticos retornam o tipo dos operandos (promovido se necessário)
                // Se havia int e float, o resultado já é float
                return left_type;
            } else if (bin->op == "==" || bin->op == "!=" || bin->op == "<" || 
                       bin->op == ">" || bin->op == "<=" || bin->op == ">=") {
                // Operadores de comparação retornam bool
                return gettyptr("bool");
            } else if (bin->op == "&&" || bin->op == "||") {
                // Operadores lógicos retornam bool
                unify_ctx.unify(left_type, gettyptr("bool"));
                return gettyptr("bool");
            }
            
            return left_type;
        }
        case NodeType::CallExpression: {
            const auto* call = static_cast<CallExprNode*>(node);
            auto func_type = infer_expr(call->caller.get());
            func_type = unify_ctx.resolve(func_type);
            
            // Se não for função, criar tipo de função com variáveis de tipo
            if (func_type->kind != Kind::LABEL) {
                // Tentar unificar com tipo de função
                auto ret_type = unify_ctx.new_type_var();
                std::vector<std::shared_ptr<Type>> param_types;
                for (const auto& arg : call->args) {
                    param_types.push_back(infer_expr(arg.get()));
                }
                auto expected_func = std::make_shared<Label>(param_types, ret_type);
                try {
                    unify_ctx.unify(func_type, expected_func);
                } catch (std::runtime_error& e) {
                    throw std::runtime_error("Call expression type error: " + std::string(e.what()));
                }
                func_type = unify_ctx.resolve(func_type);
                if (func_type->kind == Kind::LABEL) {
                    auto label = std::static_pointer_cast<Label>(func_type);
                    return label->returntype;
                }
            } else {
                auto label = std::static_pointer_cast<Label>(func_type);
                
                // Verificar se é uma função builtin com argumentos opcionais
                bool is_builtin_varargs = false;
                std::string func_name;
                if (call->caller->kind == NodeType::Identifier) {
                    auto* id = static_cast<IdentifierNode*>(call->caller.get());
                    func_name = id->symbol;
                    // Verificar se é builtin com varargs
                    auto it = std::find_if(BUILTIN_FUNCTIONS.begin(), BUILTIN_FUNCTIONS.end(),
                        [&func_name](const BuiltinFunction& b) { return b.name == func_name; });
                    if (it != BUILTIN_FUNCTIONS.end() && it->accepts_varargs) {
                        is_builtin_varargs = true;
                        if (!builtin_accepts_args(*it, call->args.size())) {
                            throw std::runtime_error("Function '" + func_name + "' argument count mismatch: " +
                                                    std::to_string(it->min_args) + " to " + 
                                                    (it->max_args == 0 ? "unlimited" : std::to_string(it->max_args)) +
                                                    " expected, got " + std::to_string(call->args.size()));
                        }
                    }
                }
                
                // Verificar número de argumentos
                if (!is_builtin_varargs && call->args.size() != label->paramstype.size()) {
                    throw std::runtime_error("Function call argument count mismatch: expected " + 
                                            std::to_string(label->paramstype.size()) + 
                                            ", got " + std::to_string(call->args.size()));
                }
                
                // Unificar tipos dos argumentos com tipos dos parâmetros
                // Para funções com varargs, unificar apenas os argumentos fornecidos
                if (call->args.size() > 0) {
                    size_t max_args = std::min(call->args.size(), label->paramstype.size());
                    for (size_t i = 0; i < max_args; i++) {
                        auto arg_type = infer_expr(call->args[i].get());
                        try {
                            unify_ctx.unify(arg_type, label->paramstype[i]);
                        } catch (std::runtime_error& e) {
                            throw std::runtime_error("Function call argument type error: " + std::string(e.what()));
                        }
                    }
                }
                return label->returntype;
            }
            
            return unify_ctx.new_type_var();
        }
        case NodeType::ArrayExpression: {
            const auto* arr = static_cast<ArrayExprNode*>(node);
            if (arr->elements.empty()) {
                // Array vazio - criar variável de tipo para elemento
                auto elem_type = unify_ctx.new_type_var();
                return std::make_shared<Array>(elem_type);
            }
            
            // Inferir tipo do primeiro elemento
            auto first_type = infer_expr(arr->elements[0].get());
            
            // Unificar todos os elementos com o primeiro
            for (size_t i = 1; i < arr->elements.size(); i++) {
                auto elem_type = infer_expr(arr->elements[i].get());
                try {
                    unify_ctx.unify(first_type, elem_type);
                } catch (std::runtime_error& e) {
                    throw std::runtime_error("Array element type error: " + std::string(e.what()));
                }
            }
            
            first_type = unify_ctx.resolve(first_type);
            return std::make_shared<Array>(first_type);
        }
        case NodeType::TupleExpression: {
            const auto* tup = static_cast<TupleExprNode*>(node);
            std::vector<std::shared_ptr<Type>> elem_types;
            for (const auto& elem : tup->elements) {
                elem_types.push_back(infer_expr(elem.get()));
            }
            return std::make_shared<Tuple>(elem_types);
        }
        case NodeType::AssignmentExpression: {
            const auto* assign = static_cast<AssignmentExprNode*>(node);
            auto left_type = infer_expr(assign->target.get());
            auto right_type = infer_expr(assign->value.get());
            
            // Resolver tipos antes de unificar
            left_type = unify_ctx.resolve(left_type);
            right_type = unify_ctx.resolve(right_type);
            
            // Verificar coerção implícita int -> float
            bool left_is_int = left_type->kind == Kind::INT;
            bool left_is_float = left_type->kind == Kind::FLOAT;
            bool right_is_int = right_type->kind == Kind::INT;
            bool right_is_float = right_type->kind == Kind::FLOAT;
            
            // Se um é int e outro é float, promover int para float
            if (left_is_int && right_is_float) {
                left_type = gettyptr("float");
            } else if (left_is_float && right_is_int) {
                right_type = gettyptr("float");
            }
            
            try {
                unify_ctx.unify(left_type, right_type);
            } catch (std::runtime_error& e) {
                throw std::runtime_error("Assignment type error: " + std::string(e.what()));
            }
            
            // Resolver tipo após unificação
            right_type = unify_ctx.resolve(right_type);
            return right_type;
        }
        default:
            // Para outros tipos, usar verificação tradicional
            return check_node(node);
    }
}