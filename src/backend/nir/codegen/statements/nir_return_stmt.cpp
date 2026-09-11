#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/return_stmt_node.hpp"

void ReturnStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());

    // A @[no_std] program is linked with -Wl,-e,<entry>, so the entry function has no
    // caller: `return` in it would fall into whatever follows the entry point (every
    // no_std binary used to print its output and then die with SIGSEGV, rc 139). The
    // return value becomes the exit code instead — the syscall does not come back, so
    // the return below is unreachable and only gives the block its terminator.
    // `naked_asm def _start` never reaches here: that body is raw asm, not Narval.
    if (ctx.in_no_std_entry() && value) {
        value->nir_codegen(ctx);
        mlir::Value ret = ctx.pop_value();
        if (ret) {
            auto vt = ctx.get_narval_value_type();
            // OpBuilder does not advance its insertion point, so consecutive creates
            // come out reversed unless the point sits at the end of the block (the
            // exit call has to come FIRST, then the return).
            b.setInsertionPointToEnd(b.getBlock());
            ctx.ensure_runtime_func("_exit",
                mlir::FunctionType::get(&ctx.get_mlir_context(), {vt}, {}));
            mlir::narval::CallRuntimeOp::create(
                b, loc, mlir::TypeRange{},
                mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), "_exit"),
                mlir::ValueRange{ret});
            mlir::narval::ReturnOp::create(b, loc, mlir::ValueRange{ret});
            return;
        }
        mlir::narval::ReturnOp::create(b, loc, mlir::ValueRange{});
        return;
    }

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
