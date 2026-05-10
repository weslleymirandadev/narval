#pragma once
#include "frontend/ast/types.hpp"
#include <vector>
#include <string>
#include <memory>

namespace nv { class IRGenerationContext; }

// Representa atributos de módulo: @[attr1, attr2, ...]
// Devem aparecer no topo do arquivo, antes de qualquer statement.
// Controlam quais partes do runtime são incluídas no binário final.
//
// Atributos suportados:
//   minimal          — apenas core (create_*, aritmética); implica todos os no_*
//   no_exceptions    — sem try/catch/throw
//   no_traceback     — sem nv_push/pop_frame
//   no_write         — sem write()
//   no_read          — sem read()
//   no_strings       — sem métodos de string
//   no_vectors       — sem métodos de vector
//   no_maps          — sem métodos de map
//   no_option_result — sem Some/None/Ok/Err
//   strip            — --strip-all no binário final (remove debug)
//   lto              — habilita link-time optimization
struct ModuleAttrNode : public Stmt {
    std::vector<std::string> attrs;

    ModuleAttrNode() : Stmt(NodeType::ModuleAttrStatement) {}

    Node* clone() const override {
        auto* n = new ModuleAttrNode();
        n->attrs = attrs;
        if (position) n->position = std::make_unique<PositionData>(*position);
        return n;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};
