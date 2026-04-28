#include "frontend/checker/expressions/check_new_expr.hpp"
#include "frontend/ast/expressions/new_expr_node.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/checker/type.hpp"
#include "frontend/ast/ast.hpp"
#include <string>

namespace nv {
    std::shared_ptr<Type> check_new_expr(Checker* checker, Node* node) {
        auto* new_expr = static_cast<NewExprNode*>(node);
        
        // Obter o tipo da classe
        auto class_type = checker->gettyptr(new_expr->class_name);
        
        // Verificar se é realmente uma classe
        if (class_type->kind != Kind::CLASS) {
            checker->error(node, "'" + new_expr->class_name + "' is not a class");
            return checker->gettyptr("void");
        }
        
        // Verificar argumentos do construtor (por enquanto, não verificamos)
        // TODO: Implementar verificação de argumentos do construtor
        
        // Retornar o tipo da classe
        return class_type;
    }
}
