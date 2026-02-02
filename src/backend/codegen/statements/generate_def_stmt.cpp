#include "frontend/ast/statements/def_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <llvm/IR/Verifier.h>
#include <llvm/IR/DIBuilder.h>

void DefStmtNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    // Preserve current codegen state
    llvm::Function* prev_func = ctx.get_current_function();
    llvm::BasicBlock* prev_insert_block = ctx.get_builder().GetInsertBlock();
    llvm::DIScope* prev_scope = ctx.get_debug_scope();

    std::vector<llvm::Type*> param_types;
    std::vector<std::string> param_names;
    for (auto& p : parameters) {
        for (auto& kv : p.parameter) {
            param_names.push_back(kv.first);
            param_types.push_back(nv::ir_utils::llvm_type_from_string(ctx, kv.second));
        }
    }
    llvm::Type* ret_ty = nv::ir_utils::llvm_type_from_string(ctx, return_type);
    auto* fn_ty = llvm::FunctionType::get(ret_ty, param_types, false);
    auto* fn = llvm::Function::Create(fn_ty, llvm::Function::ExternalLinkage, name, ctx.get_module());

    if (auto* dib = ctx.get_debug_builder()) {
        llvm::DIFile* file = ctx.get_debug_file();

        unsigned line = position ? static_cast<unsigned>(position->line) : 0u;

        auto* sub_ty = dib->createSubroutineType(dib->getOrCreateTypeArray({}));
        auto* subp = dib->createFunction(
            file,                  // <-- sempre o arquivo, não o escopo atual
            name,
            llvm::StringRef(),
            file,
            line,
            sub_ty,
            line,
            llvm::DINode::FlagZero,
            llvm::DISubprogram::SPFlagDefinition
        );
        fn->setSubprogram(subp);
        ctx.set_debug_scope(subp);
    }

    // Register function symbol at current (likely global) scope so it can be referenced by name
    nv::SymbolInfo fn_info(fn, fn->getType(), nullptr, false, true);
    ctx.get_symbol_table().define_symbol(name, fn_info);
    
    // IMPORTANTE: Se esta função foi importada com um alias, também atualizar a entrada do alias
    // Verificar se há alguma entrada na tabela de símbolos com nullptr que deveria apontar para esta função
    // Isso acontece quando fazemos "import teste as nigger" e depois a função "teste" é criada
    // Não temos acesso direto aos aliases, então vamos verificar todas as entradas no escopo atual
    // e atualizar aquelas que têm nullptr mas deveriam apontar para esta função
    // (Isso é uma heurística - idealmente teríamos um mapeamento de alias -> nome original)

    ctx.set_current_function(fn);
    auto* entry = llvm::BasicBlock::Create(ctx.get_context(), "entry", fn);
    ctx.get_builder().SetInsertPoint(entry);

    unsigned idx = 0;
    for (auto& arg : fn->args()) {
        if (idx < param_names.size()) {
            arg.setName(param_names[idx++]);
        }
    }

    ctx.enter_scope();
    if (idx) {
        idx = 0;
        for (auto& arg : fn->args()) {
            auto* alloca = ctx.create_and_register_variable(
                std::string(arg.getName()),
                arg.getType(),
                nullptr,
                false
            );
            ctx.get_builder().CreateStore(&arg, alloca);
            // Local variable debug info for parameters is temporarily disabled
            // to avoid crashes inside LLVM's DwarfDebug. The IR still has
            // function-level DISubprogram and locations.
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
    if (prev_scope) {
        ctx.set_debug_scope(prev_scope);
    }
}
