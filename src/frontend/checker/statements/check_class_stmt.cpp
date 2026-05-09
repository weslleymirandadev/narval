#include "frontend/checker/statements/check_class_stmt.hpp"
#include "frontend/ast/statements/class_stmt_node.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/checker/type.hpp"
#include "frontend/ast/ast.hpp"
#include <string>
#include <sstream>

namespace nv {
    std::shared_ptr<Type> check_class_stmt(Checker* checker, Node* node) {
        auto* class_stmt = static_cast<ClassStmtNode*>(node);
        
        // Criar tipo Class
        auto class_type = std::make_shared<Class>(class_stmt->name);
        
        // Registrar a classe no escopo atual e no mapa de tipos (para gettyptr)
        checker->types[class_stmt->name] = class_type;
        checker->scope->put_key(class_stmt->name, class_type, true);
        
        // Se tiver classe pai, configurar herança
        if (!class_stmt->parent_class.empty()) {
            auto parent_type = checker->gettyptr(class_stmt->parent_class);
            
            // Permitir herança de classes ou tipos builtin
            if (parent_type->kind == Kind::CLASS) {
                class_type->parent_class = std::static_pointer_cast<Class>(parent_type);
            } else if (parent_type->kind == Kind::INT || 
                      parent_type->kind == Kind::FLOAT || 
                      parent_type->kind == Kind::STRING ||
                      parent_type->kind == Kind::BOOL ||
                      parent_type->kind == Kind::ARRAY ||
                      parent_type->kind == Kind::VECTOR ||
                      parent_type->kind == Kind::MAP ||
                      parent_type->kind == Kind::TUPLE) {
                // Para tipos builtin, criar uma classe wrapper que herda as propriedades
                // Por enquanto, tratamos como se fosse uma classe normal
                class_type->parent_builtin_type = parent_type;
                class_type->is_builtin_derived = true;
            } else {
                checker->error(node, "Parent class '" + class_stmt->parent_class + "' is not a class or builtin type");
            }
        }
        
        // Adicionar campos
        for (const auto& field : class_stmt->fields) {
            auto field_type = checker->gettyptr(field->type);
            class_type->add_field(field->name, field_type, field->is_mutable);
        }

        // Definir contexto de classe para verificação de acesso em corpos de métodos
        std::string previous_class = checker->current_class_name;
        checker->current_class_name = class_stmt->name;

        // Adicionar métodos com assinatura real a partir do DefStmtNode
        for (const auto& method : class_stmt->methods) {
            std::vector<std::shared_ptr<Type>> param_types;
            std::shared_ptr<Type> ret_type = std::make_shared<Void>();

            if (method->method_def) {
                auto* function = static_cast<FunctionStmtNode*>(method->method_def.get());
                for (const auto& pn : function->parameters) {
                    for (const auto& kv : pn.parameter) {
                        param_types.push_back(checker->gettyptr(kv.second));
                    }
                }
                if (!function->return_type.empty() && function->return_type != "void" &&
                    function->return_type != "automatic") {
                    ret_type = checker->gettyptr(function->return_type);
                }
            }

            auto method_type = std::make_shared<Function>(param_types, ret_type);
            class_type->add_method(method->name, method_type, method->access_modifier);
        }

        checker->current_class_name = previous_class;

        // Verificar que todos os métodos das interfaces implementadas estão presentes
        // e promovê-los automaticamente a public (interface = contrato público)
        for (const auto& iface_name : class_stmt->implements_interfaces) {
            auto it = checker->types.find(iface_name);
            if (it == checker->types.end() || it->second->kind != Kind::INTERFACE) {
                checker->error(node, "'" + iface_name + "' não é uma interface");
                continue;
            }
            auto* iface = static_cast<nv::Interface*>(it->second.get());
            auto required = iface->all_methods(checker->types);
            for (const auto& [mname, mtype] : required) {
                bool found = class_type->methods.find(mname) != class_type->methods.end();
                if (!found && class_type->parent_class) {
                    found = class_type->parent_class->get_method(mname) != nullptr;
                }
                if (!found) {
                    checker->error(node,
                        "Classe '" + class_stmt->name + "' não implementa o método '" +
                        mname + "' exigido pela interface '" + iface_name + "'");
                } else {
                    // Métodos que implementam interface são automaticamente públicos
                    class_type->method_access[mname] = "public";
                }
            }
        }

        // Inicializar prototype da classe
        class_type->init_prototype();

        return class_type;
    }
}
