#pragma once
#include "frontend/ast/ast.hpp"
#include <string>
#include <vector>


struct ExternFromImportItem {
    std::string name;   // nome original: "pyplot", "sin"
    std::string alias;  // alias opcional: "plt" (vazio se não houver)
};

// `from extern "lang:lib" import items`
//
// Formas suportadas:
//   from extern "Python:matplotlib.pyplot" import * as plt  → namespace proxy
//   from extern "Python:numpy"             import array, zeros
//   from extern "C:math"                   import sin, cos
//   from extern "C:stdlib"                 import *
class ExternFromImportStmtNode : public Stmt {
public:
    std::string language;                         // "Python", "C"
    std::string library;                          // "matplotlib.pyplot", "math"
    std::vector<ExternFromImportItem> imports;    // lista de itens
    bool        is_wildcard    = false;           // import *
    std::string wildcard_alias;                   // alias do namespace (import * as plt)

    ExternFromImportStmtNode(std::string lang, std::string lib,
                              std::vector<ExternFromImportItem> items,
                              bool wildcard = false, std::string walias = "")
        : Stmt(NodeType::ExternFromImportStatement),
          language(std::move(lang)), library(std::move(lib)),
          imports(std::move(items)),
          is_wildcard(wildcard), wildcard_alias(std::move(walias)) {}

    ~ExternFromImportStmtNode() override = default;

    Node* clone() const override {
        auto* n = new ExternFromImportStmtNode(
            language, library, imports, is_wildcard, wildcard_alias);
        if (position) n->position = std::make_unique<PositionData>(*position);
        return n;
    }

    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};
