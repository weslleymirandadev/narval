#include "frontend/ast/statements/label_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <llvm/IR/Verifier.h>

void LabelStmtNode::codegen(rph::IRGenerationContext& ctx) {
    // Preserve current codegen state
    llvm::Function* prev_func = ctx.get_current_function();
    llvm::BasicBlock* prev_insert_block = ctx.get_builder().GetInsertBlock();

    std::vector<llvm::Type*> param_types;
    std::vector<std::string> param_names;
    for (auto& p : parameters) {
        for (auto& kv : p.parameter) {
            param_names.push_back(kv.first);
            param_types.push_back(rph::ir_utils::llvm_type_from_string(ctx, kv.second));
        }
    }
    llvm::Type* ret_ty = rph::ir_utils::llvm_type_from_string(ctx, return_type);
    auto* fn_ty = llvm::FunctionType::get(ret_ty, param_types, false);
    auto* fn = llvm::Function::Create(fn_ty, llvm::Function::ExternalLinkage, name, ctx.get_module());

    // Register function symbol at current (likely global) scope so it can be referenced by name
    rph::SymbolInfo fn_info(fn, fn->getType(), nullptr, false, true);
    ctx.get_symbol_table().define_symbol(name, fn_info);

    ctx.set_current_function(fn);
    auto* entry = llvm::BasicBlock::Create(ctx.get_context(), "entry", fn);
    ctx.get_builder().SetInsertPoint(entry);

    unsigned idx = 0;
    for (auto& arg : fn->args()) {
        if (idx < param_names.size()) arg.setName(param_names[idx++]);
    }

    ctx.enter_scope();
    if (idx) {
        idx = 0;
        for (auto& arg : fn->args()) {
            auto* alloca = ctx.create_and_register_variable(std::string(arg.getName()), arg.getType(), nullptr, false);
            ctx.get_builder().CreateStore(&arg, alloca);
        }
    }
    for (auto& stmt : body) {
        if (stmt) stmt->codegen(ctx);
    }
    ctx.exit_scope();

    if (!entry->getTerminator()) {
        if (ret_ty->isVoidTy()) ctx.get_builder().CreateRetVoid();
        else ctx.get_builder().CreateRet(llvm::UndefValue::get(ret_ty));
    }

    // Restore previous codegen state so following nodes are emitted into the original function/scope
    ctx.set_current_function(prev_func);
    if (prev_insert_block) {
        ctx.get_builder().SetInsertPoint(prev_insert_block);
    }
}
