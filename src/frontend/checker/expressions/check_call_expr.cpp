#include "frontend/checker/expressions/check_call_expr.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/checker/builtins.hpp"
#include "frontend/checker/type.hpp"
#include <algorithm>
#include <sstream>

std::shared_ptr<nv::Type>& check_call_expr(nv::Checker* ch, Node* node) {
    const auto* call = static_cast<CallExprNode*>(node);

    // Tratamento especial para chamadas de método: obj.method(args)
    if (call->caller->kind == NodeType::MemberExpression) {
        auto* member_expr = static_cast<MemberExprNode*>(call->caller.get());

        // Verificar se o objeto é um namespace alias de wildcard import
        if (member_expr->object->kind == NodeType::Identifier &&
            member_expr->property->kind == NodeType::Identifier) {
            auto* obj_id = static_cast<IdentifierNode*>(member_expr->object.get());
            auto ns_it = ch->import_namespaces.find(obj_id->symbol);
            if (ns_it != ch->import_namespaces.end()) {
                auto* prop_id = static_cast<IdentifierNode*>(member_expr->property.get());
                auto mem_it = ns_it->second.find(prop_id->symbol);
                if (mem_it != ns_it->second.end()) {
                    auto method_type = ch->unify_ctx.resolve(mem_it->second);
                    if (method_type->kind == nv::Kind::FUNCTION) {
                        auto function = std::static_pointer_cast<nv::Function>(method_type);
                        for (const auto& arg : call->args) {
                            ch->infer_expr(arg.get());
                        }
                        return function->returntype;
                    }
                    // Membro não é função: retorna o tipo mesmo assim (p.ex. variável)
                    return mem_it->second;
                }
                ch->error(member_expr->property.get(),
                          "Namespace '" + obj_id->symbol + "' has no member '" + prop_id->symbol + "'");
                return ch->gettyptr("void");
            }
        }

        // Verificar tipo do objeto
        auto object_type = ch->infer_expr(member_expr->object.get());
        object_type = ch->unify_ctx.resolve(object_type);
        
        // Verificar que a propriedade é um identificador
        if (member_expr->property->kind != NodeType::Identifier) {
            ch->error(member_expr->property.get(), 
                      "Method name must be an identifier");
            return ch->gettyptr("void");
        }
        
        auto* prop_id = static_cast<IdentifierNode*>(member_expr->property.get());
        const std::string& method_name = prop_id->symbol;
        
        // Verificar se o objeto tem o método
        std::shared_ptr<nv::Type> method_type = nullptr;
        if (object_type->kind == nv::Kind::CLASS) {
            // Usar Class::get_method que percorre a cadeia de herança corretamente
            auto* class_type = static_cast<nv::Class*>(object_type.get());
            method_type = class_type->get_method(method_name);

            // Verificar visibilidade
            if (method_type && !class_type->is_method_accessible(method_name, ch->current_class_name)) {
                ch->error(member_expr->property.get(),
                          "Method '" + method_name + "' is private in class '" + class_type->name + "'");
                return ch->gettyptr("void");
            }
        } else {
            if (!object_type->prototype) {
                try { object_type->init_prototype(); } catch (...) {}
            }
            try {
                method_type = object_type->get_method(method_name);
            } catch (...) {}
        }

        if (!method_type) {
            ch->error(member_expr->property.get(),
                      "Type '" + object_type->toString() + "' does not have method '" + method_name + "'");
            return ch->gettyptr("void");
        }
        
        // Verificar se é uma função
        if (method_type->kind != nv::Kind::FUNCTION) {
            ch->error(member_expr->property.get(), 
                      "'" + method_name + "' is not a method");
            return ch->gettyptr("void");
        }
        
        auto function = std::static_pointer_cast<nv::Function>(method_type);
        
        // Verificar cada argumento
        std::vector<std::shared_ptr<nv::Type>> arg_types;
        for (const auto& arg : call->args) {
            auto arg_type = ch->infer_expr(arg->value.get());
            arg_types.push_back(arg_type);
        }
        
        if (ch->err) {
            return ch->gettyptr("void");
        }
        
        // Verificar número de argumentos
        if (call->args.size() != function->paramstype.size()) {
            std::ostringstream oss;
            oss << "Method call argument count mismatch: expected " 
                << function->paramstype.size() 
                << ", got " << call->args.size();
            ch->error(const_cast<Node*>(node), oss.str());
            return ch->gettyptr("void");
        }
        
        // Unificar tipos dos argumentos
        for (size_t i = 0; i < call->args.size(); i++) {
            try {
                ch->unify_ctx.unify(arg_types[i], function->paramstype[i]);
            } catch (std::runtime_error& e) {
                std::ostringstream oss;
                oss << "Method call argument type error: " << e.what();
                ch->error(const_cast<Node*>(node), oss.str());
                return ch->gettyptr("void");
            }
        }
        
        return function->returntype;
    }
    
    // Verificar o caller (função sendo chamada) usando infer_expr
    // Isso já verifica identificadores corretamente
    auto func_type = ch->infer_expr(call->caller.get());
    bool caller_has_error = ch->err;
    
    // Verificar cada argumento recursivamente usando infer_expr
    // Isso garante que todos os identificadores sejam verificados corretamente
    // e também faz a inferência de tipos necessária
    // IMPORTANTE: Continuar verificando argumentos mesmo se houver erro no caller
    // para reportar todos os erros, não apenas o primeiro
    // O conjunto reported_errors já previne duplicação de erros, então não precisamos
    // resetar err - apenas continuar verificando mesmo com err=true
    std::vector<std::shared_ptr<nv::Type>> arg_types;
    for (const auto& arg : call->args) {
        auto arg_type = ch->infer_expr(arg->value.get());
        arg_types.push_back(arg_type);
    }
    
    // Se há erros (no caller ou nos argumentos), retornar imediatamente
    // após ter verificado todos os argumentos para reportar todos os erros
    if (ch->err) {
        return ch->gettyptr("void");
    }
    
    func_type = ch->unify_ctx.resolve(func_type);
    
    // Se não for função, criar tipo de função com variáveis de tipo
    if (func_type->kind != nv::Kind::FUNCTION) {
        // Tentar unificar com tipo de função
        auto ret_type = ch->unify_ctx.new_type_var();
        auto expected_func = std::make_shared<nv::Function>(arg_types, ret_type);
        try {
            ch->unify_ctx.unify(func_type, expected_func);
        } catch (std::runtime_error& e) {
            std::ostringstream oss;
            oss << "Call expression type error: " << e.what();
            ch->error(const_cast<Node*>(node), oss.str());
            return ch->gettyptr("void");
        }
        func_type = ch->unify_ctx.resolve(func_type);
        if (func_type->kind == nv::Kind::FUNCTION) {
            auto function = std::static_pointer_cast<nv::Function>(func_type);
            return function->returntype;
        }
    } else {
        auto function = std::static_pointer_cast<nv::Function>(func_type);
        
        // Verificar se é uma função builtin com argumentos opcionais
        bool is_builtin_varargs = false;
        std::string func_name;
        if (call->caller->kind == NodeType::Identifier) {
            auto* id = static_cast<IdentifierNode*>(call->caller.get());
            func_name = id->symbol;
            // Verificar se é builtin com varargs
            auto it = std::find_if(nv::BUILTIN_FUNCTIONS.begin(), nv::BUILTIN_FUNCTIONS.end(),
                [&func_name](const nv::BuiltinFunction& b) { return b.name == func_name; });
            if (it != nv::BUILTIN_FUNCTIONS.end() && it->accepts_varargs) {
                is_builtin_varargs = true;
                if (!nv::builtin_accepts_args(*it, call->args.size())) {
                    std::ostringstream oss;
                    oss << "Function '" << func_name << "' argument count mismatch: " 
                        << it->min_args << " to " 
                        << (it->max_args == 0 ? "unlimited" : std::to_string(it->max_args))
                        << " expected, got " << call->args.size();
                    ch->error(const_cast<Node*>(node), oss.str());
                    return ch->gettyptr("void");
                }
            }
        }
        
        // Verificar número de argumentos
        // Support keyword arguments for regular functions
        bool has_keywords = false;
        for (const auto& a : call->args) if (!a->name.empty()) { has_keywords = true; break; }

        if (!is_builtin_varargs && !has_keywords) {
            // Check argument count considering default values
            std::string func_name;
            if (call->caller->kind == NodeType::Identifier) {
                func_name = static_cast<IdentifierNode*>(call->caller.get())->symbol;
            }
            auto defaults_it = ch->function_param_defaults.find(func_name);
            size_t min_args = function->paramstype.size();
            if (defaults_it != ch->function_param_defaults.end()) {
                const auto& param_defaults = defaults_it->second;
                // Count how many parameters from the end have defaults
                while (min_args > 0 && min_args - 1 < param_defaults.size() && param_defaults[min_args - 1]) {
                    min_args--;
                }
            }
            
            if (call->args.size() > function->paramstype.size() || call->args.size() < min_args) {
                std::ostringstream oss;
                if (call->args.size() < min_args) {
                    oss << "Function call argument count mismatch: expected at least " << min_args;
                } else {
                    oss << "Function call argument count mismatch: expected at most " << function->paramstype.size();
                }
                oss << ", got " << call->args.size();
                ch->error(const_cast<Node*>(node), oss.str());
                return ch->gettyptr("void");
            }
        }
        
        // Unificar tipos dos argumentos com tipos dos parâmetros
        // Para funções com varargs, unificar apenas os argumentos fornecidos
        if (!has_keywords) {
            if (call->args.size() > 0) {
                size_t max_args = std::min(call->args.size(), function->paramstype.size());
                for (size_t i = 0; i < max_args; i++) {
                    try {
                        ch->unify_ctx.unify(arg_types[i], function->paramstype[i]);
                    } catch (std::runtime_error& e) {
                        std::ostringstream oss;
                        oss << "Function call argument type error: " << e.what();
                        ch->error(const_cast<Node*>(node), oss.str());
                        return ch->gettyptr("void");
                    }
                }
            }
        } else {
            // Map keyword args to parameter order using recorded parameter names
            if (call->caller->kind != NodeType::Identifier) {
                ch->error(const_cast<Node*>(node), "Keyword arguments only supported for direct function identifiers");
                return ch->gettyptr("void");
            }
            auto* id = static_cast<IdentifierNode*>(call->caller.get());
            auto it = ch->function_param_names.find(id->symbol);
            if (it == ch->function_param_names.end()) {
                ch->error(const_cast<Node*>(node), "Cannot bind keyword arguments: unknown function parameter names");
                return ch->gettyptr("void");
            }
            const auto& pname_list = it->second;
            if (pname_list.size() != function->paramstype.size()) {
                ch->error(const_cast<Node*>(node), "Internal error: parameter name list size mismatch");
                return ch->gettyptr("void");
            }

            std::vector<std::shared_ptr<nv::Type>> ordered_args(function->paramstype.size());
            std::vector<bool> assigned(function->paramstype.size(), false);
            size_t pos_index = 0;
            bool seen_keyword = false;
            for (const auto& a : call->args) {
                if (a->name.empty()) {
                    if (seen_keyword) {
                        ch->error(const_cast<Node*>(node), "Positional argument after keyword argument is not allowed");
                        return ch->gettyptr("void");
                    }
                    if (pos_index >= ordered_args.size()) {
                        ch->error(const_cast<Node*>(node), "Too many positional arguments");
                        return ch->gettyptr("void");
                    }
                    ordered_args[pos_index++] = ch->infer_expr(a->value.get());
                    assigned[pos_index-1] = true;
                } else {
                    seen_keyword = true;
                    auto found = std::find(pname_list.begin(), pname_list.end(), a->name);
                    if (found == pname_list.end()) {
                        ch->error(const_cast<Node*>(node), "Unknown parameter name '" + a->name + "'");
                        return ch->gettyptr("void");
                    }
                    size_t idx = std::distance(pname_list.begin(), found);
                    if (assigned[idx]) {
                        ch->error(const_cast<Node*>(node), "Duplicate argument for parameter '" + a->name + "'");
                        return ch->gettyptr("void");
                    }
                    ordered_args[idx] = ch->infer_expr(a->value.get());
                    assigned[idx] = true;
                }
            }

            // Ensure all required parameters were provided (check defaults)
            auto defaults_it = ch->function_param_defaults.find(id->symbol);
            const auto& param_defaults = (defaults_it != ch->function_param_defaults.end()) ? 
                                        defaults_it->second : std::vector<bool>(assigned.size(), false);
            
            for (size_t i = 0; i < assigned.size(); ++i) {
                if (!assigned[i]) {
                    // Check if this parameter has a default value
                    if (i < param_defaults.size() && param_defaults[i]) {
                        // Parameter has default value, it's optional
                        continue;
                    }
                    ch->error(const_cast<Node*>(node), "Missing argument for parameter '" + pname_list[i] + "'");
                    return ch->gettyptr("void");
                }
            }

            // Unify types in order (skip positions that were not provided and have defaults)
            for (size_t i = 0; i < ordered_args.size(); ++i) {
                if (!ordered_args[i]) continue; // omitted param with default — skip unification
                try {
                    ch->unify_ctx.unify(ordered_args[i], function->paramstype[i]);
                } catch (std::runtime_error& e) {
                    std::ostringstream oss;
                    oss << "Function call argument type error: " << e.what();
                    ch->error(const_cast<Node*>(node), oss.str());
                    return ch->gettyptr("void");
                }
            }
        }
        return function->returntype;
    }
    
    // Fallback: se não conseguimos determinar o tipo da função, retornar void
    // Isso não deveria acontecer em código válido, mas serve como fallback seguro
    return ch->gettyptr("void");
}
