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
            if (parent_type->kind == Kind::CLASS) {
                class_type->parent_class = std::static_pointer_cast<Class>(parent_type);
            } else {
                checker->error(node, "Parent class '" + class_def->parent_class + "' is not a class");
            }
        }
        
        // Adicionar campos
        for (const auto& field : class_def->fields) {
            auto field_type = checker->gettyptr(field->type);
            class_type->add_field(field->name, field_type, field->is_mutable);
        }
        
        // Adicionar métodos (por enquanto, criamos tipos básicos para métodos)
        for (const auto& method : class_def->methods) {
            // Por enquanto, métodos são tratados como funções void sem parâmetros
            auto method_type = std::make_shared<Def>(
                std::vector<std::shared_ptr<Type>>{},
                std::make_shared<Void>()
            );
            class_type->add_method(method->name, method_type);
        }
        
        // Inicializar prototype da classe
        class_type->init_prototype();
        
        return class_type;
    }
}
