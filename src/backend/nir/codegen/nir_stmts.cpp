#ifdef NARVAL_USE_NIR

#include "backend/nir/NIRGenerationContext.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
#include "frontend/ast/statements/return_stmt_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/statements/if_statement_node.hpp"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/Builders.h"

void FunctionStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());
    auto  val = ctx.get_narval_value_type();

    // Build function type: all params are !narval.value; return is also !narval.value
    // (except void functions which have no return value).
    bool is_void = (return_type == "None" || return_type.empty());
    std::vector<mlir::Type> param_types(parameters.size(), val);
    mlir::TypeRange ret_types = is_void ? mlir::TypeRange{} : mlir::TypeRange{val};

    auto fn_type = mlir::FunctionType::get(&ctx.get_mlir_context(), param_types, ret_types);
    auto fn = mlir::func::FuncOp::create(b, loc, name, fn_type);
    fn.setPublic();

    auto* entry = fn.addEntryBlock();
    b.setInsertionPointToStart(entry);

    // Register function in module scope before generating body (supports recursion).
    ctx.push_scope();

    // Bind argument names to MLIR block args.
    size_t idx = 0;
    for (const auto& param : parameters) {
        for (const auto& [pname, _] : param.parameter) {
            if (idx < fn.getNumArguments())
                ctx.define(pname, fn.getArgument(idx));
            ++idx;
        }
    }

    // Emit body.
    for (const auto& stmt : body)
        if (stmt) stmt->nir_codegen(ctx);

    // If the last op in entry is not a return, add one.
    if (entry->empty() || !entry->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
        if (is_void) {
            mlir::func::ReturnOp::create(b, loc);
        } else {
            // Return a zero/undef value as placeholder.
            auto undef = mlir::arith::ConstantOp::create(b, loc,
                b.getI32Type(), b.getI32IntegerAttr(0));
            // Box it as narval.value — placeholder until expression codegen is complete.
            mlir::func::ReturnOp::create(b, loc, mlir::ValueRange{});
        }
    }

    ctx.pop_scope();
    ctx.set_current_func(fn);
}

void ReturnStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());

    if (value) {
        value->nir_codegen(ctx);
        // Result is in ctx's value stack (once expression codegen is wired).
        // For now emit void return as placeholder.
    }
    mlir::func::ReturnOp::create(b, loc);
}


void IfStatementNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    // Placeholder: narval.if emission requires condition as i1.
    // Will be wired once expression codegen is implemented.
    // For now: emit the branches as-is (may produce invalid IR for complex cases).
    for (const auto& stmt : consequent)
        if (stmt) stmt->nir_codegen(ctx);
}

#endif // NARVAL_USE_NIR
