#pragma once
#include "frontend/ast/types.hpp"
#include <string>
#include <vector>
#include <memory>

namespace nv { class IRGenerationContext; }

enum class AsmConstraintKind { Reg };

struct AsmInputConstraint {
    AsmConstraintKind kind = AsmConstraintKind::Reg;
    std::string var_name;
};

struct AsmOutputBinding {
    std::string from_var;   // registrador de onde vem (mesmo nome do input)
    std::string result_var; // variável criada no escopo após o asm
};

class InlineAsmStmtNode : public Stmt {
public:
    std::string asm_template;
    std::vector<AsmInputConstraint> inputs;
    std::vector<AsmOutputBinding>   outputs;

    InlineAsmStmtNode() : Stmt(NodeType::InlineAsmStatement) {}

    Node* clone() const override; // implementado em check_inline_asm_stmt.cpp (key function para vtable)
    void codegen(nv::IRGenerationContext& ctx) override;
    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};
