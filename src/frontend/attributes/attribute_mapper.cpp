#include "frontend/attributes/attribute_mapper.hpp"
#include "frontend/ast/program.hpp"
#include "frontend/ast/statements/attribute_stmt_node.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
#include "frontend/ast/statements/module_attr_node.hpp"
#include <algorithm>

namespace nv {

namespace {

void apply_attribute(CompilationAttributes& attrs, const std::string& name, Node* node) {
    if (name == "no_std") {
        attrs.no_std = true;
        if (!attrs.no_std_node) {
            attrs.no_std_node = node;
        }
    } else if (name == "strip") {
        attrs.strip = true;
    } else if (name == "lto") {
        attrs.lto = true;
    } else if (std::find(attrs.unknown.begin(), attrs.unknown.end(), name) == attrs.unknown.end()) {
        attrs.unknown.push_back(name);
    }
}

void map_from_program(CompilationAttributes& attrs, Program* program) {
    for (const auto& stmt : program->body) {
        if (!stmt) {
            continue;
        }
        if (stmt->kind == NodeType::AttributeStatement) {
            auto* attr = static_cast<AttributeStmtNode*>(stmt.get());
            for (const auto& name : attr->attrs) {
                apply_attribute(attrs, name, stmt.get());
            }
        } else if (stmt->kind == NodeType::ModuleAttrStatement) {
            auto* attr = static_cast<ModuleAttrNode*>(stmt.get());
            for (const auto& name : attr->attrs) {
                apply_attribute(attrs, name, stmt.get());
            }
        }
    }
}

} // namespace

CompilationAttributes map_compilation_attributes(Node* node) {
    CompilationAttributes attrs;
    if (!node) {
        return attrs;
    }
    if (node->kind == NodeType::Program) {
        map_from_program(attrs, static_cast<Program*>(node));
    } else if (node->kind == NodeType::AttributeStatement) {
        auto* attr = static_cast<AttributeStmtNode*>(node);
        for (const auto& name : attr->attrs) {
            apply_attribute(attrs, name, node);
        }
    } else if (node->kind == NodeType::ModuleAttrStatement) {
        auto* attr = static_cast<ModuleAttrNode*>(node);
        for (const auto& name : attr->attrs) {
            apply_attribute(attrs, name, node);
        }
    }
    return attrs;
}

bool program_has_function(Node* node, const std::string& name) {
    if (!node || node->kind != NodeType::Program) {
        return false;
    }
    auto* program = static_cast<Program*>(node);
    for (const auto& stmt : program->body) {
        if (!stmt || stmt->kind != NodeType::FunctionStatement) {
            continue;
        }
        auto* fn = static_cast<FunctionStmtNode*>(stmt.get());
        if (fn->name == name) {
            return true;
        }
    }
    return false;
}

} // namespace nv
