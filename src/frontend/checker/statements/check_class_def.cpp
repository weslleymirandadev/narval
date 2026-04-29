#include "frontend/checker/statements/check_class_def.hpp"
#include "frontend/ast/statements/class_def_node.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/checker/type.hpp"
#include "frontend/ast/ast.hpp"
#include <string>

namespace nv {
    std::shared_ptr<Type> check_class_def(Checker* checker, Node* node) {
        auto* class_def = static_cast<ClassDefNode*>(node);
        
        // Criar tipo Class
        auto class_type = std::make_shared<Class>(class_def->name);
        
        // Registrar a classe no escopo atual e no mapa de tipos (para gettyptr)
        checker->types[class_def->name] = class_type;
        checker->scope->put_key(class_def->name, class_type, true);
        
        // Se tiver classe pai, configurar herança
        if (!class_def->parent_class.empty()) {
            auto parent_type = checker->gettyptr(class_def->parent_class);
            
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
                checker->error(node, "Parent class '" + class_def->parent_class + "' is not a class or builtin type");
            }
        }
        
        // Adicionar campos
        for (const auto& field : class_def->fields) {
            auto field_type = checker->gettyptr(field->type);
            class_type->add_field(field->name, field_type, field->is_mutable);
        }

        // Definir contexto de classe para verificação de acesso em corpos de métodos
        std::string previous_class = checker->current_class_name;
        checker->current_class_name = class_def->name;

        // Adicionar métodos com assinatura real a partir do DefStmtNode
        for (const auto& method : class_def->methods) {
            std::vector<std::shared_ptr<Type>> param_types;
            std::shared_ptr<Type> ret_type = std::make_shared<Void>();

            if (method->method_def) {
                auto* def = static_cast<DefStmtNode*>(method->method_def.get());
                for (const auto& pn : def->parameters) {
                    for (const auto& kv : pn.parameter) {
                        param_types.push_back(checker->gettyptr(kv.second));
                    }
                }
                if (!def->return_type.empty() && def->return_type != "void" &&
                    def->return_type != "automatic") {
                    ret_type = checker->gettyptr(def->return_type);
                }
            }

            auto method_type = std::make_shared<Def>(param_types, ret_type);
            class_type->add_method(method->name, method_type, method->access_modifier);
        }

        checker->current_class_name = previous_class;
        
        // Inicializar prototype da classe
        class_type->init_prototype();
        
        return class_type;
    }
}
