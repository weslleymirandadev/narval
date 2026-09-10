#include "../nir_codegen_utils.hpp"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "frontend/ast/statements/function_stmt_node.hpp"

void FunctionStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());
    auto  vt  = ctx.get_narval_value_type();

    // Save the caller's insertion point so top-level code after this function
    // definition continues to be emitted in the right block (e.g. main.start).
    mlir::OpBuilder::InsertionGuard guard(b);

    bool is_void = (return_type == "None" || return_type == "void" || return_type.empty());
    std::vector<mlir::Type> param_types(parameters.size(), vt);
    // NOTE: never build this from `TypeRange{vt}` / braced-init: TypeRange
    // stores an ArrayRef pointing at the temporary initializer-list backing
    // array, which dies at the end of the full expression → the FunctionType
    // is uniqued with a dangling result type (garbage impl pointer) that
    // crashes later passes (SymbolDCE walk, type printing). Use a vector.
    llvm::SmallVector<mlir::Type> ret_types;
    if (!is_void) ret_types.push_back(vt);

    auto fn_type = mlir::FunctionType::get(&ctx.get_mlir_context(), param_types, ret_types);

    // Move builder to module level before creating the function so it isn't
    // nested inside main.start. Save and restore the caller's insertion point.
    mlir::OpBuilder::InsertionGuard guard2(b);
    b.setInsertionPointToEnd(ctx.get_module().getBody());

    // Calling a function that is defined later in the module creates a
    // placeholder declaration; drop it, otherwise emitting the body is reported
    // as "redefinition of symbol". Calls reference the symbol by name, so they
    // stay valid.
    if (mlir::Operation* existing = ctx.get_module().lookupSymbol(name)) {
        bool is_decl = false;
        if (auto f = mlir::dyn_cast<mlir::func::FuncOp>(existing))
            is_decl = f.isDeclaration();
        else if (auto nf = mlir::dyn_cast<mlir::narval::FuncOp>(existing))
            is_decl = nf.getBody().empty();
        if (is_decl) existing->erase();
    }

    auto fn = mlir::narval::FuncOp::create(b, loc, name, fn_type, "",
                                           false, false, false, nullptr);

    auto* entry = new mlir::Block();
    fn.getBody().push_back(entry);
    llvm::SmallVector<mlir::Location> arg_locs(param_types.size(), loc);
    entry->addArguments(param_types, arg_locs);
    b.setInsertionPointToStart(entry);

    ctx.push_scope();

    size_t idx = 0;
    for (const auto& param : parameters) {
        for (const auto& [pname, _] : param.parameter) {
            if (idx < entry->getNumArguments())
                ctx.define(pname, entry->getArgument(idx));
            ++idx;
        }
    }

    nir_emit_body(body, ctx);

    if (entry->empty() || !entry->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
        if (is_void) {
            mlir::narval::ReturnOp::create(b, loc, mlir::ValueRange{});
        } else {
            auto zero = mlir::narval::ConstantOp::create(b, loc, vt,
                b.getI64IntegerAttr(0)).getResult();
            mlir::narval::ReturnOp::create(b, loc, mlir::ValueRange{zero});
        }
    }

    ctx.pop_scope();

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
