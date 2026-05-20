#pragma once

#include "frontend/ast/ast.hpp"
#include <string>
#include <vector>

namespace nv {

struct CompilationAttributes {
    bool no_std = false;
    bool strip = false;
    bool lto = false;
    Node* no_std_node = nullptr;
    std::vector<std::string> unknown;

    bool has_any() const {
        return no_std || strip || lto || !unknown.empty();
    }
};

CompilationAttributes map_compilation_attributes(Node* node);
bool program_has_function(Node* node, const std::string& name);
// Retorna o nome da naked_asm function que serve de entry point, ou "" se não houver
std::string program_naked_asm_entry(Node* node);

} // namespace nv
