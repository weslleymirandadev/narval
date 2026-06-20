#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/return_stmt_node.hpp"

void ReturnStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());

    if (value) {
        value->nir_codegen(ctx);
        mlir::Value ret = ctx.pop_value();
        if (ret) {
            mlir::narval::ReturnOp::create(b, loc, mlir::ValueRange{ret});
            return;
        }
    }
    mlir::narval::ReturnOp::create(b, loc, mlir::ValueRange{});
}
