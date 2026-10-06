#include "backend/nir/NIRGenerationContext.hpp"
#include "nir_codegen_utils.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/ast/program.hpp"
#include "frontend/ast/statements/module_attr_node.hpp"

#include "backend/nir/NirDiagnostics.hpp"
#include <cstdlib>

namespace nv {

void generate_ir_nir(std::unique_ptr<Node> node, NIRGenerationContext& ctx) {
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

    // Toda classe declara o seu ctor e os seus metodos ANTES de qualquer corpo ser emitido. Sem isso,
    // uma funcao declarada acima da classe nao encontrava `__ctor_A` e `new A(5)` pulava a chamada do
    // construtor em silencio (campo nunca escrito, `get()` devolvendo None). Mesma forma do que a
    // propria classe faz para os irmaos dela, um nivel acima: um passo, antes dos corpos.
    for (const auto& stmt : program->body) {
        if (stmt && stmt->kind == NodeType::ClassStatement)
            nir_predeclare_class(ctx, *static_cast<ClassStmtNode*>(stmt.get()));
    }

    for (const auto& stmt : program->body) {
        if (!stmt) continue;
        stmt->nir_codegen(ctx);
    }

    ctx.pop_scope();

    // NARVAL_DUMP_NIR=1 (or --emit-nir) prints the narval-dialect module as codegen
    // left it, before any pass rewrites it — the only way to read the
    // loop/branch/ownership structure while it still looks like the source.
    if (diag_emit_nir() || std::getenv("NARVAL_DUMP_NIR")) ctx.dump_nir();
}

} // namespace nv
