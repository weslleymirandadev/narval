#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
#include "mlir/Dialect/Func/IR/FuncOps.h"

void FunctionStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());
    auto  vt  = ctx.get_narval_value_type();

    // Save the caller's insertion point so top-level code after this function
    // definition continues to be emitted in the right block (e.g. main.start).
    mlir::OpBuilder::InsertionGuard guard(b);

    bool is_void = (return_type == "None" || return_type == "void" || return_type.empty());
    std::vector<mlir::Type> param_types(parameters.size(), vt);
    mlir::TypeRange ret_types = is_void ? mlir::TypeRange{} : mlir::TypeRange{vt};

    auto fn_type = mlir::FunctionType::get(&ctx.get_mlir_context(), param_types, ret_types);
    auto fn = mlir::func::FuncOp::create(b, loc, name, fn_type);
    fn.setPublic();

    auto* entry = fn.addEntryBlock();
    b.setInsertionPointToStart(entry);

    ctx.push_scope();

    size_t idx = 0;
    for (const auto& param : parameters) {
        for (const auto& [pname, _] : param.parameter) {
            if (idx < fn.getNumArguments())
                ctx.define(pname, fn.getArgument(idx));
            ++idx;
        }
    }

    nir_emit_body(body, ctx);

    if (entry->empty() || !entry->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
        if (is_void) {
            mlir::func::ReturnOp::create(b, loc);
        } else {
            auto zero = mlir::narval::ConstantOp::create(b, loc, vt,
                b.getI64IntegerAttr(0)).getResult();
            mlir::func::ReturnOp::create(b, loc, mlir::ValueRange{zero});
        }
    }

    ctx.pop_scope();
    ctx.set_current_func(fn);
    ctx.get_module().push_back(fn);

    // Apply any pending @optimize hints as a narval.optimize DictionaryAttr.
    if (ctx.pending_optimize.has_value()) {
        auto& hints = *ctx.pending_optimize;
        auto& mlir_ctx = ctx.get_mlir_context();

        llvm::SmallVector<mlir::NamedAttribute> dict_entries;

        if (!hints.tile_sizes.empty())
            dict_entries.emplace_back(
                mlir::StringAttr::get(&mlir_ctx, "tile_sizes"),
                mlir::DenseI64ArrayAttr::get(&mlir_ctx, hints.tile_sizes));

        if (hints.vectorize)
            dict_entries.emplace_back(
                mlir::StringAttr::get(&mlir_ctx, "vectorize"),
                mlir::UnitAttr::get(&mlir_ctx));

        if (hints.parallelize)
            dict_entries.emplace_back(
                mlir::StringAttr::get(&mlir_ctx, "parallelize"),
                mlir::UnitAttr::get(&mlir_ctx));

        if (hints.unroll > 0)
            dict_entries.emplace_back(
                mlir::StringAttr::get(&mlir_ctx, "unroll"),
                mlir::IntegerAttr::get(mlir::IntegerType::get(&mlir_ctx, 64), hints.unroll));

        if (!dict_entries.empty())
            fn->setAttr("narval.optimize",
                        mlir::DictionaryAttr::get(&mlir_ctx, dict_entries));

        ctx.pending_optimize.reset();
    }
}
