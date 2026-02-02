#include <iostream>
#include <fstream>
#include <string>
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/module_manager.hpp"
#include "frontend/checker/checker.hpp"
#include "backend/codegen/generate_ir.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <filesystem>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/IR/DIBuilder.h>

extern "C" const char* nv_base_dir = nullptr; // visible to C runtime
static std::string nv_base_dir_storage;

int main(int argc, char* argv[]) {
    std::string filename;
    std::string module_name = "main";

    if (argc >= 2) {
        filename = argv[1];
    }

    if (filename.empty()) {
        std::cerr << "Usage: narval <source.nv>\n";
        return 1;
    }

    // Initialize base dir from source file path (directory containing main.nv)
    nv_base_dir_storage = std::filesystem::path(filename).parent_path().string();
    nv_base_dir = nv_base_dir_storage.c_str();

    ModuleManager module_manager;
    try {
        module_manager.compile_module(module_name, filename, true);
        auto ast = module_manager.get_combined_ast(module_name);

        // Criar checker para inferência de tipos
        nv::Checker checker;
        checker.set_source_file(filename);
        // Verificar tipos antes da geração de código
        if (ast) {
            checker.check_node(ast.get());
        }

        llvm::LLVMContext Context;
        llvm::Module Mod("narval_module", Context);
        llvm::IRBuilder<llvm::NoFolder> Builder(Context);
        nv::IRGenerationContext context(Context, Mod, Builder, &checker);

        // === Debug info setup (same as main.cpp) ===
        llvm::DIBuilder DIB(Mod);
        Mod.addModuleFlag(llvm::Module::Warning, "Debug Info Version", llvm::DEBUG_METADATA_VERSION);

        llvm::DIFile* diFile = DIB.createFile(
            filename,
            std::filesystem::path(filename).parent_path().string()
        );

        llvm::DICompileUnit* cu = DIB.createCompileUnit(
            llvm::dwarf::DW_LANG_C, // placeholder language id
            diFile,
            "narval-compiler-test",
            false,
            "",
            0
        );

        context.set_debug_info(&DIB, cu, diFile, cu);

        auto* i32_ty      = llvm::Type::getInt32Ty(Context);
        auto* main_sig    = llvm::FunctionType::get(llvm::Type::getVoidTy(Context), false);

        llvm::Function* main_start = llvm::Function::Create(
            main_sig,
            llvm::Function::ExternalLinkage,
            "main.start",
            Mod
        );

        // Attach DISubprogram to main.start for better function-level debug info
        {
            auto* sub_ty = DIB.createSubroutineType(DIB.getOrCreateTypeArray({}));
            auto* subp = DIB.createFunction(
                cu,
                "main.start",
                llvm::StringRef(),
                diFile,
                1,
                sub_ty,
                1,
                llvm::DINode::FlagZero,
                llvm::DISubprogram::SPFlagDefinition
            );
            main_start->setSubprogram(subp);
            context.set_debug_scope(subp);
        }

        llvm::BasicBlock* entry_bb = llvm::BasicBlock::Create(Context, "entry", main_start);
        context.get_builder().SetInsertPoint(entry_bb);
        context.set_current_function(main_start);

        nv::generate_ir(std::move(ast), context);
        
        // IMPORTANTE: Finalizar inicializações de globais DEPOIS de gerar o código principal
        // Isso garante que todas as declarações foram processadas
        context.finalize_global_inits(65535);
        
        // Chamar explicitamente a função de inicialização no início de main.start
        // Isso garante que os globais sejam inicializados mesmo se @llvm.global_ctors não funcionar
        // (devido ao uso de -nostartfiles e -Wl,-e,main.start)
        auto* init_func_name = "nv.global.init.65535";
        auto* init_func = Mod.getFunction(init_func_name);
        if (init_func) {
            // Salvar o ponto de inserção atual
            auto* saved_insert_point = context.get_builder().GetInsertBlock();
            auto saved_insert_iter = context.get_builder().GetInsertPoint();
            
            // Inserir a chamada no início do entry block (antes de qualquer outra instrução)
            auto* entry_block = &main_start->getEntryBlock();
            context.get_builder().SetInsertPoint(entry_block, entry_block->begin());
            context.get_builder().CreateCall(init_func);
            
            // Restaurar o ponto de inserção original
            if (saved_insert_point && saved_insert_iter != saved_insert_point->end()) {
                context.get_builder().SetInsertPoint(saved_insert_iter);
            } else if (saved_insert_point) {
                context.get_builder().SetInsertPoint(&saved_insert_point->back());
            }
        }

        llvm::Value* return_value = nullptr;
        if (context.has_value()) {
            return_value = context.pop_value();
        }
        if (!return_value) {
            return_value = llvm::ConstantInt::get(i32_ty, 0);
        }

        if (return_value->getType() != i32_ty) {
            auto* ValueTy = nv::ir_utils::get_value_struct(context);
            auto* ValuePtr = nv::ir_utils::get_value_ptr(context);
            // Check if it's a Value struct - extract the integer value from it
            if (return_value->getType() == ValueTy) {
                // Ensure the value type is correct before extracting
                auto* tmp_alloca = context.get_builder().CreateAlloca(ValueTy, nullptr, "return_val_tmp");
                context.get_builder().CreateStore(return_value, tmp_alloca);
                
                // Call ensure_value_type to guarantee the tag is correct
                auto* ensure_func = context.ensure_runtime_func("ensure_value_type", {ValuePtr});
                context.get_builder().CreateCall(ensure_func, {tmp_alloca});
                
                // Extract integer value from Value struct (field 1 contains the value as i64)
                // The type tag (field 0) should be TAG_INT after ensure_value_type
                auto* valuePtr = context.get_builder().CreateStructGEP(ValueTy, tmp_alloca, 1);
                auto* i64_ty = llvm::Type::getInt64Ty(Context);
                auto* value64 = context.get_builder().CreateLoad(i64_ty, valuePtr, "value64");
                
                // Convert to i32 (truncate from i64)
                // Note: For TAG_INT, the value is stored directly in the i64 field
                return_value = context.get_builder().CreateTrunc(value64, i32_ty, "int_val");
            } else if (return_value->getType()->isIntegerTy()) {
                return_value = context.get_builder().CreateIntCast(return_value, i32_ty, true);
            } else if (return_value->getType()->isFloatingPointTy()) {
                return_value = context.get_builder().CreateFPToSI(return_value, i32_ty);
            } else {
                return_value = llvm::ConstantInt::get(i32_ty, 0);
            }
        }

        // declare _exit(int);
        auto* exit_ty = llvm::FunctionType::get(llvm::Type::getVoidTy(Context), {i32_ty}, false);
        llvm::FunctionCallee exit_fn = Mod.getOrInsertFunction("_exit", exit_ty);

        // call _exit(retcode); no return
        context.get_builder().CreateCall(exit_fn, {return_value});
        context.get_builder().CreateUnreachable();

        DIB.finalize();

        {
            std::error_code EC;
            llvm::raw_fd_ostream ir_out("narval_module.ll", EC, llvm::sys::fs::OF_Text);
            if (!EC) {
                Mod.print(ir_out, nullptr);
            }
        }

        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();

        auto target_triple = llvm::sys::getDefaultTargetTriple();
        Mod.setTargetTriple(target_triple);

        std::string error;
        const llvm::Target* target = llvm::TargetRegistry::lookupTarget(target_triple, error);
        if (!target) {
            llvm::errs() << "Erro de target: " << error << "\n";
            return 1;
        }

        llvm::TargetOptions opt;
        std::unique_ptr<llvm::TargetMachine> target_machine(
            target->createTargetMachine(target_triple, "generic", "", opt, llvm::Reloc::PIC_)
        );

        Mod.setDataLayout(target_machine->createDataLayout());

        std::error_code EC;
        llvm::raw_fd_ostream dest("narval_module.o", EC, llvm::sys::fs::OF_None);
        if (EC) {
            llvm::errs() << "Falha ao abrir .o: " << EC.message() << "\n";
            return 1;
        }

        llvm::legacy::PassManager pass;
        if (target_machine->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
            llvm::errs() << "TargetMachine não suporta emissão de objeto\n";
            return 1;
        }
        pass.run(Mod);
        dest.flush();

        
        std::string link_cmd =
            std::string("gcc -g ") + NARVAL_SOURCE_DIR + "/build/lib/runtime.o " +
            NARVAL_SOURCE_DIR + "/build/lib/std.o " +
            "narval_module.o -lgc -pthread -ldl -lm -o narval_program " +
            "-Wl,-e,main.start " +     // entry point
            "-nostartfiles " +         // sem crt0, _start
            "-no-pie " +               // opcional
            "-lc -w";                // libc + sem warnings

        const char* rm_cmd = "rm narval_module.o narval_module.ll";

        if (system(link_cmd.c_str()) != 0) {
            llvm::errs() << "Falha na linkedição\n";
            return 1;
        }

        if (system(rm_cmd) != 0) {
            llvm::errs() << "Falha ao remover arquivos temporários\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Erro durante compilação: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
