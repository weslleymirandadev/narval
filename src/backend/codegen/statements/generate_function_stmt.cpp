#include "frontend/ast/statements/function_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/checker/checker.hpp"
#include <unordered_set>
#include <string>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/InlineAsm.h>

void FunctionStmtNode::codegen(nv::IRGenerationContext& ctx) {
    // naked_asm def NAME — emitido como module-level assembly (.intel_syntax noprefix).
    // Isso contorna os quirks do atributo naked do LLVM e passa o assembly
    // diretamente ao assembler, garantindo ausência de prólogo/epílogo.
    if (is_naked_asm) {
        auto& module = ctx.get_module();

        // Construir bloco de assembly em nível de módulo
        std::string mod_asm;
        mod_asm += ".intel_syntax noprefix\n";
        mod_asm += ".global " + name + "\n";
        mod_asm += ".type " + name + ", @function\n";
        mod_asm += name + ":\n";
        mod_asm += naked_asm_body;
        mod_asm += "\n.att_syntax prefix\n";

        // Acumular no inline asm de módulo existente (pode haver múltiplas funções naked)
        std::string existing = module.getModuleInlineAsm();
        if (!existing.empty() && existing.back() != '\n') existing += "\n";
        module.setModuleInlineAsm(existing + mod_asm);

        // Declarar o símbolo como função externa no IR para que o linker resolva referências
        auto* fn_ty = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx.get_context()), {}, false);
        if (!module.getFunction(name)) {
            auto* fn = llvm::Function::Create(
                fn_ty, llvm::Function::ExternalLinkage, name, module);
            fn->addFnAttr(llvm::Attribute::NoUnwind);
            fn->addFnAttr(llvm::Attribute::NoReturn);
        }
        return;
    }

    // Preserve current codegen state (incl. debug scope – set before any use)
    llvm::Function* prev_func = ctx.get_current_function();
    llvm::BasicBlock* prev_insert_block = ctx.get_builder().GetInsertBlock();
    llvm::DIScope* prev_scope = ctx.get_debug_scope();
    bool prev_fallible = ctx.is_current_function_fallible();
    ctx.set_current_function_fallible(is_fallible);

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
            // Tipos compostos Narval (vector, array, map, tuple) são sempre Value structs.
            // Funções @[abi] usam tipos low-level bare — não promover para Value.
            static const std::unordered_set<std::string> composite_types = {
                "vector", "array", "map", "tuple"
            };
            bool is_ll_type = (checker && [&]{
                try { return checker->gettyptr(kv.second)->kind == nv::Kind::LOW_LEVEL; }
                catch (...) { return false; }
            }());
            if (!is_ll_type && (composite_types.count(kv.second) ||
                (param_ty && (param_ty->isVoidTy() || param_ty->isPointerTy()) && kv.second != "void"))) {
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
    bool ret_is_ll = (checker && [&]{
        try { return checker->gettyptr(return_type)->kind == nv::Kind::LOW_LEVEL; }
        catch (...) { return false; }
    }());
    if (!ret_is_ll && ret_ty && ret_ty->isVoidTy() && return_type != "void") {
        ret_ty = nv::ir_utils::get_value_struct(ctx);
    }
    // Funções falíveis sempre retornam Value (que contém Result::Ok ou Result::Err)
    if (is_fallible) {
        ret_ty = nv::ir_utils::get_value_struct(ctx);
    }
    auto* fn_ty = llvm::FunctionType::get(ret_ty, param_types, false);
    
    // Tenta encontrar a função se ela já foi declarada (por exemplo, no pre-pass de assinaturas)
    auto* fn = ctx.get_module().getFunction(name);
    if (!fn) {
        fn = llvm::Function::Create(fn_ty, llvm::Function::ExternalLinkage, name, ctx.get_module());
    }

    // Aplicar calling convention quando @[abi(...)] está presente
    if (is_low_level && !abi.empty()) {
        if (abi == "sysv64")
            fn->setCallingConv(llvm::CallingConv::X86_64_SysV);
        else if (abi == "win64")
            fn->setCallingConv(llvm::CallingConv::Win64);
        else if (abi == "C" || abi == "c")
            fn->setCallingConv(llvm::CallingConv::C);
    }

    llvm::DISubprogram* subp = nullptr;
    unsigned function_line = 0u;
    if (auto* dib = ctx.get_debug_builder()) {
        llvm::DIFile* file = ctx.get_debug_file();
        function_line = position ? static_cast<unsigned>(position->line) : 0u;
        auto* sub_ty = dib->createSubroutineType(dib->getOrCreateTypeArray({}));
        subp = dib->createFunction(
            file,
            name,
            llvm::StringRef(),
            file,
            function_line,
            sub_ty,
            function_line,
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
            llvm::DILocation::get(ctx.get_context(), function_line, 1, subp));
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
    ctx.set_current_function_fallible(prev_fallible);
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
