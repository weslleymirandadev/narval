#include <iostream>
#include <fstream>
#include <string>
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/module_manager.hpp"
#include "backend/codegen/generate_ir.hpp"
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/CodeGen.h>

int main(int argc, char* argv[]) {
    std::string filename = "../test/main.ttn";
    std::string module_name = "main";

    ModuleManager module_manager;
    try {
        module_manager.compile_module(module_name, filename, true);
        auto ast = module_manager.get_combined_ast();

        llvm::LLVMContext Context;
        llvm::Module Mod("ruphi_module", Context);
        llvm::IRBuilder<llvm::NoFolder> Builder(Context);
        rph::IRGenerationContext context(Context, Mod, Builder);

        auto* i32_ty      = llvm::Type::getInt32Ty(Context);
        auto* main_sig    = llvm::FunctionType::get(llvm::Type::getVoidTy(Context), false);

        llvm::Function* main_start = llvm::Function::Create(
            main_sig,
            llvm::Function::ExternalLinkage,
            "main.start",
            Mod
        );

        llvm::BasicBlock* entry_bb = llvm::BasicBlock::Create(Context, "entry", main_start);
        context.get_builder().SetInsertPoint(entry_bb);
        context.set_current_function(main_start);

        rph::generate_ir(std::move(ast), context);

        llvm::Value* return_value = nullptr;
        if (context.has_value()) {
            return_value = context.pop_value();
        }
        if (!return_value) {
            return_value = llvm::ConstantInt::get(i32_ty, 0);
        }

        if (return_value->getType() != i32_ty) {
            if (return_value->getType()->isIntegerTy()) {
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

        Mod.print(llvm::outs(), nullptr);

        {
            std::error_code EC;
            llvm::raw_fd_ostream ir_out("ruphi_module.ll", EC, llvm::sys::fs::OF_Text);
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
        llvm::raw_fd_ostream dest("ruphi_module.o", EC, llvm::sys::fs::OF_None);
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
            std::string("gcc ") + RUPHI_SOURCE_DIR + "/build/lib/runtime.o " +
            RUPHI_SOURCE_DIR + "/build/lib/std.o " +
            "ruphi_module.o -lgc -pthread -ldl -lm -o ruphi_program " +
            "-Wl,-e,main.start " +     // entry point
            "-nostartfiles " +         // sem crt0, _start
            "-no-pie " +               // opcional
            "-lc -w";                // libc + sem warnings

        const char* rm_cmd = "rm ruphi_module.o ruphi_module.ll";

        if (system(link_cmd.c_str()) != 0) {
            llvm::errs() << "Falha na linkedição\n";
            return 1;
        }

        if (system(rm_cmd) != 0) {
            llvm::errs() << "Falha ao remover arquivos temporários\n";
            return 1;
        }

        std::cout << "Compilação concluída: ./ruphi_program\n";
    } catch (const std::exception& e) {
        std::cerr << "Erro: " << e.what() << "\n";
        return 1;
    }

    return 0;
}