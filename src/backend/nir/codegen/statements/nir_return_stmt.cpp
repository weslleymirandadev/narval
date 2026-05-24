#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/return_stmt_node.hpp"
#include "mlir/Dialect/Func/IR/FuncOps.h"

void ReturnStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());

    if (value) {
        value->nir_codegen(ctx);
        mlir::Value ret = ctx.pop_value();
        if (ret) {
            mlir::func::ReturnOp::create(b, loc, mlir::ValueRange{ret});
            return;
        }
    }
    mlir::func::ReturnOp::create(b, loc);
}
