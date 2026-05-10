#include "frontend/ast/statements/extern_stmt_node.hpp"
#include "backend/codegen/ffi.hpp"
#include "backend/codegen/ir_context.hpp"

// Delega inteiramente para o backend FFI adequado.
// Para adicionar suporte a uma nova linguagem: implemente um Backend em ffi.cpp
// e registre-o em nv::ffi::backend_for().
void ExternStmtNode::codegen(nv::IRGenerationContext& ctx) {
    nv::ffi::backend_for(language, source_file).emit(*this, ctx);
}
