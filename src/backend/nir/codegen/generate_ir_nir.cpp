#ifdef NARVAL_USE_NIR

#include "backend/codegen/generate_ir.hpp"
#include "backend/nir/NIRGenerationContext.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/ast/program.hpp"
#include "frontend/ast/statements/module_attr_node.hpp"

void generate_ir_nir(std::unique_ptr<Node> node, nv::NIRGenerationContext& ctx) {
    if (!node) return;

    Node* raw = node.release();
    Program* p = (raw && raw->kind == NodeType::Program)
                 ? static_cast<Program*>(raw) : nullptr;
    std::unique_ptr<Program> program;
    if (p) {
        program.reset(p);
    } else {
        program = std::make_unique<Program>();
        if (raw)
            program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(raw)));
    }

    ctx.push_scope();

    for (const auto& stmt : program->body) {
        if (!stmt) continue;
        stmt->nir_codegen(ctx);
    }

    ctx.pop_scope();
}

#endif // NARVAL_USE_NIR
