#include "frontend/ast/statements/def_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/checker/checker.hpp"
#include <llvm/IR/Verifier.h>
#include <llvm/IR/DIBuilder.h>

void DefStmtNode::codegen(nv::IRGenerationContext& ctx) {
    // Preserve current codegen state (incl. debug scope – set before any use)
    llvm::Function* prev_func = ctx.get_current_function();
    llvm::BasicBlock* prev_insert_block = ctx.get_builder().GetInsertBlock();
    llvm::DIScope* prev_scope = ctx.get_debug_scope();

    std::vector<llvm::Type*> param_types;
    std::vector<std::string> param_names;
    nv::Checker* checker = static_cast<nv::Checker*>(ctx.get_type_checker());

    for (auto& p : parameters) {
        for (auto& kv : p.parameter) {
            param_names.push_back(kv.first);
            llvm::Type* param_ty = nullptr;
            if (checker) {
                try {
                    auto param_nv_ty = checker->gettyptr(kv.second);
                    param_ty = ctx.nv_type_to_llvm(param_nv_ty);
                } catch (...) {
                    param_ty = nullptr;
                }
            }
            if (!param_ty) {
                param_ty = nv::ir_utils::llvm_type_from_string(ctx, kv.second);
            }
            // LLVM não permite parâmetro do tipo void.
            if (param_ty && param_ty->isVoidTy() && kv.second != "void") {
                param_ty = nv::ir_utils::get_value_struct(ctx);
            }
            param_types.push_back(param_ty);
        }
    }
    llvm::Type* ret_ty = nullptr;
    if (checker) {
        try {
            auto ret_nv_ty = checker->gettyptr(return_type);
            ret_ty = ctx.nv_type_to_llvm(ret_nv_ty);
        } catch (...) {
            ret_ty = nullptr;
        }
    }
    if (!ret_ty) {
        ret_ty = nv::ir_utils::llvm_type_from_string(ctx, return_type);
    }
    if (ret_ty && ret_ty->isVoidTy() && return_type != "void") {
        ret_ty = nv::ir_utils::get_value_struct(ctx);
    }
    auto* fn_ty = llvm::FunctionType::get(ret_ty, param_types, false);
    
    // Tenta encontrar a função se ela já foi declarada (por exemplo, no pre-pass de assinaturas)
    auto* fn = ctx.get_module().getFunction(name);
    if (!fn) {
        fn = llvm::Function::Create(fn_ty, llvm::Function::ExternalLinkage, name, ctx.get_module());
    }

    llvm::DISubprogram* subp = nullptr;
    unsigned def_line = 0u;
    if (auto* dib = ctx.get_debug_builder()) {
        llvm::DIFile* file = ctx.get_debug_file();
        def_line = position ? static_cast<unsigned>(position->line) : 0u;
        auto* sub_ty = dib->createSubroutineType(dib->getOrCreateTypeArray({}));
        subp = dib->createFunction(
            file,
            name,
            llvm::StringRef(),
            file,
            def_line,
            sub_ty,
            def_line,
            llvm::DINode::FlagZero,
            llvm::DISubprogram::SPFlagDefinition
        );
        fn->setSubprogram(subp);
        ctx.set_debug_scope(subp);
    }

    // Register function symbol at current (likely global) scope so it can be referenced by name
    nv::SymbolInfo fn_info(fn, fn->getType(), nullptr, false, true);
    ctx.get_symbol_table().define_symbol(name, fn_info);

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
    // Garantir que parâmetros e corpo usem o DISubprogram desta função (evitar wrong subprogram)
    if (subp) {
        ctx.get_builder().SetCurrentDebugLocation(
            llvm::DILocation::get(ctx.get_context(), def_line, 1, subp));
    }
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
        }
    }

    // Push frame de traceback desta função
    const std::string& src_file = ctx.get_source_file();
    if (!src_file.empty())
        nv::ir_utils::emit_push_frame(ctx, src_file, name);

    for (auto& stmt : body) {
        if (stmt) {
            if (stmt->position && !src_file.empty())
                nv::ir_utils::emit_set_line(ctx, static_cast<int>(stmt->position->line));
            stmt->codegen(ctx);
        }
    }
    ctx.exit_scope();

    auto* current_bb = ctx.get_builder().GetInsertBlock();
    if (current_bb && !current_bb->getTerminator()) {
        if (!src_file.empty()) nv::ir_utils::emit_pop_frame(ctx);
        if (ret_ty->isVoidTy()) {
            ctx.get_builder().CreateRetVoid();
        } else if (ret_ty == nv::ir_utils::get_value_struct(ctx)) {
            auto* ret_val = ctx.pop_value();
            if (ret_val) {
                ctx.get_builder().CreateRet(ret_val);
            } else {
                ctx.get_builder().CreateRet(llvm::UndefValue::get(ret_ty));
            }
        } else {
            ctx.get_builder().CreateRet(llvm::UndefValue::get(ret_ty));
        }
    }

    // Restore previous codegen state so following nodes are emitted into the original function/scope
    ctx.set_current_function(prev_func);
    if (prev_insert_block) {
        ctx.get_builder().SetInsertPoint(prev_insert_block);
    }
    if (prev_scope) {
        ctx.set_debug_scope(prev_scope);
        // Também restaurar a localização de debug no builder
        ctx.get_builder().SetCurrentDebugLocation(
            llvm::DILocation::get(ctx.get_context(), 
                                 prev_insert_block 
                                    ? (prev_insert_block->getTerminator() ? prev_insert_block->getTerminator()->getDebugLoc().getLine() : 1) 
                                    : 1, 
                                 1, 
                                 prev_scope));
    } else {
        ctx.get_builder().SetCurrentDebugLocation(nullptr);
    }
}
