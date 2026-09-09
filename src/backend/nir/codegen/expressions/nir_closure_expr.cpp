#include "../nir_codegen_utils.hpp"
#include "backend/nir/NarvalOps.h"
#include "frontend/ast/expressions/closure_expr_node.hpp"
#include "mlir/Dialect/Func/IR/FuncOps.h"

static int g_closure_counter = 0;

void ClosureExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto  loc = ctx.loc(position.get());
    auto  vt  = ctx.get_narval_value_type();
    auto& b   = ctx.get_builder();
    auto& mlir_ctx = ctx.get_mlir_context();

    // Generate a unique name for this closure function.
    std::string fn_name = "__closure_fn_" + std::to_string(g_closure_counter++);

    // All params are narval.value; captures are appended after declared params.
    std::vector<mlir::Type> param_types(parameters.size(), vt);
    for (size_t i = 0; i < captures.size(); ++i)
        param_types.push_back(vt);

    bool is_void = (return_type == "None" || return_type == "void" || return_type.empty());
    // NOTE: never build a TypeRange from an initializer list (dangling
    // ArrayRef) — collect into a vector first.
    llvm::SmallVector<mlir::Type, 1> ret_list;
    if (!is_void) ret_list.push_back(vt);

    auto fn_type = mlir::FunctionType::get(&mlir_ctx, param_types, ret_list);

    // Remember the caller's insertion point: everything below up to the body
    // emission happens inside the closure function; the nv_create_closure call
    // must be emitted back in the caller's block (never inside the closure's
    // body, which would place ops after its terminator).
    mlir::OpBuilder::InsertPoint caller_ip = b.saveInsertionPoint();

    // Create the closure function directly at module scope as a narval.func:
    // phase A (LowerNarvalFunctionsPass) converts narval.func bodies (signature
    // vt->ptr, narval.return->func.return, ...). Creating it inside the
    // enclosing body and moving it out afterwards corrupted the module's
    // symbol table (crashed later lookupSymbol calls).
    b.setInsertionPointToEnd(ctx.get_module().getBody());
    auto fn = mlir::narval::FuncOp::create(b, loc, fn_name, fn_type, "",
                                           false, false, false, nullptr);

    auto* entry = new mlir::Block();
    fn.getBody().push_back(entry);
    llvm::SmallVector<mlir::Location> arg_locs(param_types.size(), loc);
    entry->addArguments(param_types, arg_locs);
    b.setInsertionPointToStart(entry);

    ctx.push_scope();

    // Bind declared parameters.
    for (size_t i = 0; i < parameters.size(); ++i)
        ctx.define(parameters[i].first, entry->getArgument(i));

    // Bind captured variables.
    for (size_t i = 0; i < captures.size(); ++i)
        ctx.define(captures[i], entry->getArgument(parameters.size() + i));

    nir_emit_body(body, ctx);

    auto* last_bb = &fn.getBody().back();
    if (last_bb->empty() || !last_bb->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
        if (is_void) {
            mlir::narval::ReturnOp::create(b, loc, mlir::ValueRange{});
        } else {
            mlir::Value zero = ctx.has_value()
                ? ctx.pop_value()
                : mlir::narval::ConstantOp::create(b, loc, vt, b.getI64IntegerAttr(0)).getResult();
            mlir::narval::ReturnOp::create(b, loc, mlir::ValueRange{zero});
        }
    }

    ctx.pop_scope();

    // Back in the caller's block: create_closure(fn_sym, cap0, cap1, ...).
    b.restoreInsertionPoint(caller_ip);

    // Collect current-scope values for captured names.
    llvm::SmallVector<mlir::Value> captured_vals;
    for (const auto& cap : captures) {
        mlir::Value v = ctx.lookup(cap);
        if (!v) v = mlir::narval::ConstantOp::create(b, loc, vt, b.getI64IntegerAttr(0)).getResult();
        captured_vals.push_back(v);
    }

    llvm::SmallVector<mlir::Value> rt_args;
    // The closure wrapper references fn by name (a string constant proxy);
    // the runtime resolves the symbol via dlsym and snapshots the captured
    // values into cells. One nv_create_closure_cN bridge per capture count.
    auto fn_name_val = mlir::narval::ConstantOp::create(b, loc, vt,
        mlir::StringAttr::get(&mlir_ctx, fn_name)).getResult();
    rt_args.push_back(fn_name_val);
    for (auto& v : captured_vals) rt_args.push_back(v);
    std::string bridge =
        "nv_create_closure_c" + std::to_string(captured_vals.size());
    ctx.push_value(nir_call_runtime(ctx, loc, bridge, rt_args, {vt}));
}
