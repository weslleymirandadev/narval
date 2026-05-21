#include "frontend/checker/expressions/check_new_expr.hpp"
#include "frontend/ast/expressions/new_expr_node.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/checker/type.hpp"
#include "frontend/ast/ast.hpp"
#include <sstream>
#include <string>

namespace nv {
    namespace {
        bool check_constructor_arg_type(Checker* checker,
                                        Node* error_node,
                                        std::shared_ptr<Type> arg_type,
                                        std::shared_ptr<Type> param_type) {
            arg_type = checker->unify_ctx.resolve(arg_type);
            param_type = checker->unify_ctx.resolve(param_type);

            if (!arg_type || !param_type) {
                checker->error(error_node, "Constructor argument type error: unknown argument type");
                return false;
            }

            if (arg_type->kind == Kind::TYPE_VAR || param_type->kind == Kind::TYPE_VAR) {
                try {
                    checker->unify_ctx.unify(arg_type, param_type);
                    return true;
                } catch (std::runtime_error& e) {
                    checker->error(error_node, "Constructor argument type error: " + std::string(e.what()));
                    return false;
                }
            }

            if (!arg_type->equals(*param_type)) {
                checker->error(error_node,
                               "Constructor argument type error: expected '" +
                               param_type->toString() + "', got '" +
                               arg_type->toString() + "'");
                return false;
            }

            return true;
        }
    }

    std::shared_ptr<Type> check_new_expr(Checker* checker, Node* node) {
        auto* new_expr = static_cast<NewExprNode*>(node);
        
        // Obter o tipo da classe
        auto class_type = checker->gettyptr(new_expr->class_name, node);
        
        // Verificar se é realmente uma classe
        if (class_type->kind != Kind::CLASS) {
            checker->error(node, "'" + new_expr->class_name + "' is not a class");
            return checker->gettyptr("None");
        }
        
        auto class_obj = std::static_pointer_cast<Class>(class_type);
        if (class_obj->is_abstract) {
            checker->error(node, "Cannot instantiate abstract class '" + new_expr->class_name + "'");
            return checker->gettyptr("None");
        }

        auto ctor_type = class_obj->get_method("new");
        if (!ctor_type) {
            if (!new_expr->arguments.empty()) {
                checker->error(node,
                               "Constructor call argument count mismatch: expected 0, got " +
                               std::to_string(new_expr->arguments.size()));
            }
            return class_type;
        }

        ctor_type = checker->unify_ctx.resolve(ctor_type);
        if (ctor_type->kind != Kind::FUNCTION) {
            checker->error(node, "'" + new_expr->class_name + ".new' is not a constructor");
            return checker->gettyptr("None");
        }

        auto ctor_fn = std::static_pointer_cast<Function>(ctor_type);
        if (new_expr->arguments.size() != ctor_fn->paramstype.size()) {
            std::ostringstream oss;
            oss << "Constructor call argument count mismatch: expected "
                << ctor_fn->paramstype.size() << ", got "
                << new_expr->arguments.size();
            checker->error(node, oss.str());
            return class_type;
        }

        for (size_t i = 0; i < new_expr->arguments.size(); ++i) {
            auto arg_type = checker->infer_expr(new_expr->arguments[i].get());
            if (!check_constructor_arg_type(checker,
                                            new_expr->arguments[i].get(),
                                            arg_type,
                                            ctor_fn->paramstype[i])) {
                return class_type;
            }
        }
        
        // Retornar o tipo da classe
        return class_type;
    }
}
