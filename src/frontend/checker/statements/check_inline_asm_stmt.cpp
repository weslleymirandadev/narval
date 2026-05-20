#include "frontend/checker/statements/check_inline_asm_stmt.hpp"
#include "frontend/ast/statements/inline_asm_stmt_node.hpp"
#include "frontend/checker/type.hpp"

// Stub weak: permite linkar sem libgenerator (ex: narval-lsp).
// A implementação real em generate_inline_asm_stmt.cpp sobrescreve este símbolo.
__attribute__((weak)) void InlineAsmStmtNode::codegen(nv::IRGenerationContext&) {}

// out-of-line clone — key function: a vtable de InlineAsmStmtNode fica neste TU (libchecker)
Node* InlineAsmStmtNode::clone() const {
    auto* n = new InlineAsmStmtNode();
    n->asm_template = asm_template;
    n->inputs  = inputs;
    n->outputs = outputs;
    if (position) n->position = std::make_unique<PositionData>(*position);
    return n;
}

std::shared_ptr<nv::Type>& check_inline_asm_stmt(nv::Checker* checker, Node* node) {
    auto* asm_node = static_cast<InlineAsmStmtNode*>(node);

    for (auto& in : asm_node->inputs) {
        if (!checker->scope->get_key(in.var_name)) {
            checker->error(node, "variável '" + in.var_name + "' não declarada (usada em constraint de asm)");
        }
    }

    for (auto& out : asm_node->outputs) {
        bool found = false;
        for (auto& in : asm_node->inputs) {
            if (in.var_name == out.from_var) { found = true; break; }
        }
        if (!found) {
            checker->error(node, "'" + out.from_var + "' em output não aparece em 'input'");
        }

        // Tipo do result = tipo do from_var no escopo
        std::shared_ptr<nv::Type> result_type = checker->gettyptr("i64");
        for (auto& in : asm_node->inputs) {
            if (in.var_name == out.from_var) {
                auto ty = checker->scope->get_key(in.var_name);
                if (ty) result_type = ty;
                break;
            }
        }
        checker->scope->put_key(out.result_var, result_type, false);
    }

    return checker->gettyptr("void");
}
