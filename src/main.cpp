#include <iostream>
#include <fstream>
#include <string>
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/module_manager.hpp"
#include "frontend/checker/checker.hpp"
#include "backend/codegen/generate_ir.hpp"
#include "backend/codegen/ir_utils.hpp"

// Novo sistema REPL implementado
#include "frontend/interactive/repl.hpp"

// Notebook mode (Jupyter-like)
#include "frontend/interactive/notebook.hpp"
// TODO: Implementar modo Notebook no futuro
// #include "frontend/interactive/notebook.hpp"

// Inicialização do runtime
#include "backend/runtime/nv_runtime.h"

#include <filesystem>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/IR/DIBuilder.h>
#include <sstream>
#include <vector>
#include <map>

extern "C" const char* nv_base_dir = nullptr; // visible to C runtime
static std::string nv_base_dir_storage;

// Função para executar modo batch (compilação normal)
int run_batch_mode(const std::string& filename) {
    // Use the file stem as the initial module name (consistent with Lexer)
    std::string module_name = std::filesystem::path(filename).stem().string();

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

        // Abort if semantic errors were found
        if (checker.err) {
            return 1;
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
        context.set_program_function(main_start);

        // Chamar register_global_init primeiro para inicializar os tipos
        auto* register_init_fn = Mod.getFunction("register_global_init");
        if (!register_init_fn) {
            auto* void_ty = llvm::Type::getVoidTy(Context);
            auto* fn_ty = llvm::FunctionType::get(void_ty, false);
            register_init_fn = llvm::Function::Create(
                fn_ty,
                llvm::Function::ExternalLinkage,
                "register_global_init",
                &Mod
            );
        }
        context.get_builder().CreateCall(register_init_fn, {});

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
            
            // Restaurar o ponto de inserção original (nunca usar back() em bloco vazio, ex.: after_bb do for)
            if (saved_insert_point) {
                if (saved_insert_iter != saved_insert_point->end()) {
                    context.get_builder().SetInsertPoint(saved_insert_iter);
                } else {
                    context.get_builder().SetInsertPoint(saved_insert_point);
                }
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
            // Check if it's a Value struct - extract the value manually
            if (return_value->getType() == ValueTy) {
                // Para a nova estrutura, declarar função manualmente
                auto* funcType = llvm::FunctionType::get(i32_ty, {ValuePtr}, false);
                auto* extract_func = llvm::cast<llvm::Function>(Mod.getOrInsertFunction("extract_int_from_value", funcType).getCallee());
                
                auto* tmp_alloca = context.get_builder().CreateAlloca(ValueTy, nullptr, "return_val_tmp");
                context.get_builder().CreateStore(return_value, tmp_alloca);
                return_value = context.get_builder().CreateCall(extract_func, {tmp_alloca}, "exit_code");
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

        // Salvar LLVM IR para debug - ANTES da verificação
        std::string ll_path = "/home/bacal/projects/cpp/narval/build/narval_module.ll";
        std::error_code ec;
        llvm::raw_fd_ostream ll_file(ll_path, ec);
        if (!ec) {
            Mod.print(ll_file, nullptr);
        }

        if (llvm::verifyModule(Mod, &llvm::errs())) {
            llvm::errs() << "IR verification failed\n";
            return 1;
        }
        
        llvm::legacy::PassManager pass;
        if (target_machine->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
            llvm::errs() << "TargetMachine não suporta emissão de objeto\n";
            return 1;
        }
        pass.run(Mod);
        dest.flush();

        
        std::string runtime_path;
        // Verificar variável de ambiente NARVAL_HOME primeiro
        const char* narval_home = std::getenv("NARVAL_HOME");
        if (narval_home) {
            runtime_path = std::string(narval_home) + "/runtime.o";
        } else {
            // Usar caminho padrão baseado no modo de build
            #ifdef NARVAL_DEV_MODE
                runtime_path = std::string(NARVAL_SOURCE_DIR) + "/build/lib/runtime.o";
            #else
                runtime_path = "/usr/lib/narval/runtime.o";
            #endif
        }
        
        std::string link_cmd =
            std::string("gcc -g ") + runtime_path + " " +
            NARVAL_SOURCE_DIR + "/build/lib/std.o " +
            "narval_module.o -lgc -pthread -ldl -lm -o narval_program " +
            "-Wl,-e,main.start " +     // entry point
            "-nostartfiles " +         // sem crt0, _start
            "-no-pie " +               // opcional
            "-lc -w";                // libc + sem warnings

        if (system(link_cmd.c_str()) != 0) {
            llvm::errs() << "Falha na linkedição\n";
            return 1;
        }
        
        // IGNORAR VERIFICAÇÃO PARA DEBUG
        // if (llvm::verifyModule(Mod, &llvm::errs())) {
        //     llvm::errs() << "IR verification failed\n";
        //     return 1;
        // }
    } catch (const std::exception& e) {
        std::cerr << "Erro durante compilação: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

// Função para executar modo REPL usando novo sistema JIT
int run_repl_mode() {
    try {
        // Inicializar base dir como diretório atual
        nv_base_dir_storage = std::filesystem::current_path().string();
        nv_base_dir = nv_base_dir_storage.c_str();
        
        // Configuração do REPL
        nv::REPLConfig config;
        config.enable_readline = true;  // Será desabilitado automaticamente se não disponível
        config.show_prompt = true;
        config.show_errors = true;
        config.show_warnings = true;
        
        // Criar e inicializar o REPL
        auto repl = std::make_unique<nv::REPL>(config);
        
        if (!repl->initialize()) {
            std::cerr << "Failed to initialize REPL" << std::endl;
            return 1;
        }
        
        // Iniciar o loop interativo
        repl->run();
        
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Erro ao inicializar REPL: " << e.what() << std::endl;
        return 1;
    }
}

// TODO: Implementar modo Notebook no futuro
// Esta função será implementada quando o módulo notebook estiver completo
/*
// Função para executar modo Notebook
int run_notebook_mode() {
    std::cout << "Modo Notebook ainda não implementado.\n";
    std::cout << "Use --repl para modo interativo.\n";
    return 1;
}
*/

int run_notebook_mode() {
    try {
        nv::REPLConfig cfg;
        cfg.enable_readline = true;
        nv::Notebook nb(cfg);
        if (!nb.initialize()) {
            std::cerr << "Failed to initialize Notebook" << std::endl;
            return 1;
        }
        nb.run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Erro ao iniciar Notebook: " << e.what() << std::endl;
        return 1;
    }
}

int main(int argc, char* argv[]) {
<<<<<<< HEAD
    // Inicializar o sistema de tipos do runtime antes de tudo
    extern void register_global_init(void);
    register_global_init();
    
=======
>>>>>>> 7d7b28c04a119a9c000597cd586b6688408f92d1
    // Parse argumentos de linha de comando
    bool repl_mode = false;
    bool notebook_mode = false;
    std::string filename;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--repl" || arg == "-i" || arg == "-r") {
            repl_mode = true;
        } else if (arg == "--notebook" || arg == "-n") {
            notebook_mode = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Uso: narval [opções] [arquivo.nv]\n";
            std::cout << "\nOpções:\n";
            std::cout << "  --repl, -i, -r     Iniciar REPL interativo com JIT\n";
            std::cout << "  --notebook, -n     Iniciar Notebook interativo\n";
            std::cout << "  --help, -h          Mostrar esta ajuda\n";
            std::cout << "\nREPL Commands:\n";
            std::cout << "  :help          - Show available commands\n";
            std::cout << "  :quit, :exit   - Exit REPL\n";
            std::cout << "  :reset         - Reset REPL context\n";
            std::cout << "  :vars          - Show defined variables\n";
            std::cout << "  :history       - Show command history\n";
            std::cout << "  :load <file>   - Load and execute file\n";
            std::cout << "  :save <file>   - Save command history\n";
            std::cout << "\nNotebook Commands (when running with --notebook):\n";
            std::cout << "  :new           - Create a new cell (end with a single . on a line)\n";
            std::cout << "  :list          - List notebook cells\n";
            std::cout << "  :run <id>      - Execute a specific cell by number\n";
            std::cout << "  :runall        - Execute all cells in order\n";
            std::cout << "  :del <id>      - Delete a specific cell by number\n";
            std::cout << "  :save <file>   - Save notebook to file (JSON if available)\n";
            std::cout << "  :load <file>   - Load notebook from file\n";
            std::cout << "  :exit          - Exit the notebook\n";
            return 0;
        } else if (arg[0] != '-') {
            // Argumento posicional (nome de arquivo)
            filename = arg;
        }
    }
    
    // Determinar modo de execução
    if (notebook_mode) {
        return run_notebook_mode();
    } else if (repl_mode) {
        return run_repl_mode();
    } else if (!filename.empty()) {
        return run_batch_mode(filename);
    } else {
        std::cerr << "Uso: narval [--repl] [--notebook] [arquivo.nv]\n";
        std::cerr << "Use --help para mais informações.\n";
        return 1;
    }
}
