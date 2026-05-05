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
                    if (method_type->kind == nv::Kind::DEF) {
                        auto def = std::static_pointer_cast<nv::Def>(method_type);
                        for (const auto& arg : call->args) {
                            ch->infer_expr(arg.get());
                        }
                        return def->returntype;
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
        if (method_type->kind != nv::Kind::DEF) {
            ch->error(member_expr->property.get(), 
                      "'" + method_name + "' is not a method");
            return ch->gettyptr("void");
        }
        
        auto def = std::static_pointer_cast<nv::Def>(method_type);
        
        // Verificar cada argumento
        std::vector<std::shared_ptr<nv::Type>> arg_types;
        for (const auto& arg : call->args) {
            auto arg_type = ch->infer_expr(arg.get());
            arg_types.push_back(arg_type);
        }
        
        if (ch->err) {
            return ch->gettyptr("void");
        }
        
        // Verificar número de argumentos
        if (call->args.size() != def->paramstype.size()) {
            std::ostringstream oss;
            oss << "Method call argument count mismatch: expected " 
                << def->paramstype.size() 
                << ", got " << call->args.size();
            ch->error(const_cast<Node*>(node), oss.str());
            return ch->gettyptr("void");
        }
        
        // Unificar tipos dos argumentos
        for (size_t i = 0; i < call->args.size(); i++) {
            try {
                ch->unify_ctx.unify(arg_types[i], def->paramstype[i]);
            } catch (std::runtime_error& e) {
                std::ostringstream oss;
                oss << "Method call argument type error: " << e.what();
                ch->error(const_cast<Node*>(node), oss.str());
                return ch->gettyptr("void");
            }
        }
        
        return def->returntype;
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
        // Usar infer_expr para verificar recursivamente cada argumento
        // Isso vai garantir que identificadores dentro de expressões sejam verificados
        // e que os tipos sejam inferidos corretamente
        // O método error() já previne duplicação usando reported_errors
        auto arg_type = ch->infer_expr(arg.get());
        
        // Continuar verificando outros argumentos mesmo se houver erro
        // para reportar todos os erros possíveis
        arg_types.push_back(arg_type);
    }
    
    // Se há erros (no caller ou nos argumentos), retornar imediatamente
    // após ter verificado todos os argumentos para reportar todos os erros
    if (ch->err) {
        return ch->gettyptr("void");
    }
    
    func_type = ch->unify_ctx.resolve(func_type);
    
    // Se não for função, criar tipo de função com variáveis de tipo
    if (func_type->kind != nv::Kind::DEF) {
        // Tentar unificar com tipo de função
        auto ret_type = ch->unify_ctx.new_type_var();
        auto expected_func = std::make_shared<nv::Def>(arg_types, ret_type);
        try {
            ch->unify_ctx.unify(func_type, expected_func);
        } catch (std::runtime_error& e) {
            std::ostringstream oss;
            oss << "Call expression type error: " << e.what();
            ch->error(const_cast<Node*>(node), oss.str());
            return ch->gettyptr("void");
        }
        func_type = ch->unify_ctx.resolve(func_type);
        if (func_type->kind == nv::Kind::DEF) {
            auto def = std::static_pointer_cast<nv::Def>(func_type);
            return def->returntype;
        }
    } else {
        auto def = std::static_pointer_cast<nv::Def>(func_type);
        
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
        if (!is_builtin_varargs && call->args.size() != def->paramstype.size()) {
            std::ostringstream oss;
            oss << "Function call argument count mismatch: expected " 
                << def->paramstype.size() 
                << ", got " << call->args.size();
            ch->error(const_cast<Node*>(node), oss.str());
            return ch->gettyptr("void");
        }
        
        // Unificar tipos dos argumentos com tipos dos parâmetros
        // Para funções com varargs, unificar apenas os argumentos fornecidos
        if (call->args.size() > 0) {
            size_t max_args = std::min(call->args.size(), def->paramstype.size());
            for (size_t i = 0; i < max_args; i++) {
                try {
                    ch->unify_ctx.unify(arg_types[i], def->paramstype[i]);
                } catch (std::runtime_error& e) {
                    std::ostringstream oss;
                    oss << "Function call argument type error: " << e.what();
                    ch->error(const_cast<Node*>(node), oss.str());
                    return ch->gettyptr("void");
                }
            }
        }
        return def->returntype;
    }
    
    // Fallback: se não conseguimos determinar o tipo da função, retornar void
    // Isso não deveria acontecer em código válido, mas serve como fallback seguro
    return ch->gettyptr("void");
}
