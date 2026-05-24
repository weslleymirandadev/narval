#pragma once
#include "frontend/ast/ast.hpp"
#include "frontend/ast/expressions/param_node.hpp"
#include <vector>
#include <string>
#include <memory>

namespace nv { class IRGenerationContext; }

// Declaração de função externa em um bloco `extern "<lang>"`.
struct ExternFuncDecl {
    std::string name;
    std::vector<ParamNode> parameters;
    std::string return_type;

    ExternFuncDecl(std::string n, std::vector<ParamNode> p, std::string rt)
        : name(std::move(n)), parameters(std::move(p)), return_type(std::move(rt)) {}
};

// `extern "C" { def foo(x: int): int; ... }` — declara funções externas.
// Para Python: `extern "Python" from "arquivo.py" { def func(): tipo }` gera o bridge automaticamente.
struct ExternStmtNode : public Stmt {
    std::string language;                        // "C", "C++", "Assembly", "Rust", "Go", "Python"
    std::string source_file;                     // Python only: caminho do .py (extern "Python" from "x.py")
    std::vector<ExternFuncDecl> declarations;

    ExternStmtNode(std::string lang, std::vector<ExternFuncDecl> decls)
        : Stmt(NodeType::ExternStatement),
          language(std::move(lang)),
          declarations(std::move(decls)) {}

    ~ExternStmtNode() override = default;

    Node* clone() const override {
        auto* n = new ExternStmtNode(language, declarations);
        n->source_file = source_file;
        if (position) n->position = std::make_unique<PositionData>(*position);
        return n;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};
