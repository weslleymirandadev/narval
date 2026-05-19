#include "frontend/checker/statements/check_class_stmt.hpp"
#include "frontend/ast/statements/class_stmt_node.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/checker/type.hpp"
#include "frontend/ast/ast.hpp"
#include <string>
#include <sstream>

namespace {
const PositionData* class_parent_position_or_class(const ClassStmtNode* class_stmt) {
    if (class_stmt->parent_position) {
        return class_stmt->parent_position.get();
    }
    if (class_stmt->position) {
        return class_stmt->position.get();
    }
    return nullptr;
}

void report_parent_error(nv::Checker* checker, Node* node, const ClassStmtNode* class_stmt,
                         const std::string& message) {
    const auto* pos = class_parent_position_or_class(class_stmt);
    if (pos) {
        checker->error_at(pos->filename, pos->line, pos->col[0], pos->col[1], message);
    } else {
        checker->error(node, message);
    }
}

const PositionData* class_implements_position_or_class(const ClassStmtNode* class_stmt, size_t index) {
    if (index < class_stmt->implements_positions.size() && class_stmt->implements_positions[index]) {
        return class_stmt->implements_positions[index].get();
    }
    if (class_stmt->position) {
        return class_stmt->position.get();
    }
    return nullptr;
}

void report_implements_error(nv::Checker* checker, Node* node, const ClassStmtNode* class_stmt,
                             size_t index, const std::string& message) {
    const auto* pos = class_implements_position_or_class(class_stmt, index);
    if (pos) {
        checker->error_at(pos->filename, pos->line, pos->col[0], pos->col[1], message);
    } else {
        checker->error(node, message);
    }
}

bool method_signature_matches(nv::Checker* checker,
                              std::shared_ptr<nv::Type> inherited_type,
                              std::shared_ptr<nv::Type> method_type) {
    inherited_type = checker->unify_ctx.resolve(inherited_type);
    method_type = checker->unify_ctx.resolve(method_type);

    if (!inherited_type || !method_type) {
        return false;
    }

    return method_type->equals(*inherited_type);
}
}

namespace nv {
    std::shared_ptr<Type> check_class_stmt(Checker* checker, Node* node) {
        auto* class_stmt = static_cast<ClassStmtNode*>(node);

        auto class_type = std::make_shared<Class>(class_stmt->name);
        class_type->is_abstract = class_stmt->is_abstract;

        checker->types[class_stmt->name] = class_type;
        checker->scope->put_key(class_stmt->name, class_type, true);

        if (!class_stmt->parent_class.empty()) {
            auto parent_it = checker->types.find(class_stmt->parent_class);
            if (parent_it == checker->types.end()) {
                report_parent_error(
                    checker,
                    node,
                    class_stmt,
                    "Unknown parent class '" + class_stmt->parent_class + "'");
            } else {
                auto parent_type = parent_it->second;

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
                    class_type->parent_builtin_type = parent_type;
                    class_type->is_builtin_derived = true;
                } else {
                    report_parent_error(
                        checker,
                        node,
                        class_stmt,
                        "Parent class '" + class_stmt->parent_class + "' is not a class or builtin type");
                }
            }
        }

        for (const auto& field : class_stmt->fields) {
            auto field_type = checker->gettyptr(field->type);
            class_type->add_field(field->name, field_type, field->is_mutable);
        }

        std::string previous_class = checker->current_class_name;
        checker->current_class_name = class_stmt->name;

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

            if (method->name != "new" && class_type->parent_class) {
                auto inherited_method = class_type->parent_class->get_method(method->name);
                if (method->is_override) {
                    if (!inherited_method) {
                        checker->error(node,
                            "Method '" + method->name + "' is marked override but does not override a parent method");
                    } else if (!method_signature_matches(checker, inherited_method, method_type)) {
                        checker->error(node,
                            "Method '" + method->name + "' is marked override but its signature does not match the parent method");
                    }
                } else if (class_type->parent_class->is_abstract && inherited_method) {
                    checker->error(node,
                        "Method '" + method->name + "' overrides abstract parent method; add 'override'");
                }
            } else if (method->is_override && method->name != "new") {
                checker->error(node,
                    "Method '" + method->name + "' is marked override but class has no parent");
            }

            class_type->add_method(method->name, method_type, method->access_modifier);
        }

        checker->current_class_name = previous_class;

        for (size_t iface_index = 0; iface_index < class_stmt->implements_interfaces.size(); ++iface_index) {
            const auto& iface_name = class_stmt->implements_interfaces[iface_index];
            auto it = checker->types.find(iface_name);
            if (it == checker->types.end() || it->second->kind != Kind::INTERFACE) {
                report_implements_error(
                    checker,
                    node,
                    class_stmt,
                    iface_index,
                    "'" + iface_name + "' is not an interface");
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
                        "Class '" + class_stmt->name + "' does not implement method '" +
                        mname + "' required by interface '" + iface_name + "'");
                } else {
                    class_type->method_access[mname] = "public";
                }
            }
        }

        class_type->init_prototype();

        return class_type;
    }
}
