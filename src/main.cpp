#include <iostream>
#include <fstream>
#include <string>
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/module_manager.hpp"
#include "frontend/checker/checker.hpp"
#include "backend/codegen/generate_ir.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/interactive/interactive_session.hpp"
#include "frontend/interactive/session_manager.hpp"
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
#include <sstream>
#include <vector>
#include <map>

extern "C" const char* nv_base_dir = nullptr; // visible to C runtime
static std::string nv_base_dir_storage;

// Função para executar modo batch (compilação normal)
int run_batch_mode(const std::string& filename) {
    std::string module_name = "main";

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
            // Check if it's a Value struct - extract the value based on its type tag
            if (return_value->getType() == ValueTy) {
                // Ensure the value type is correct before extracting
                auto* tmp_alloca = context.get_builder().CreateAlloca(ValueTy, nullptr, "return_val_tmp");
                context.get_builder().CreateStore(return_value, tmp_alloca);
                
                // Call ensure_value_type to guarantee the tag is correct
                auto* ensure_func = context.ensure_runtime_func("ensure_value_type", {ValuePtr});
                context.get_builder().CreateCall(ensure_func, {tmp_alloca});
                
                // Extract type tag (field 0) to determine how to extract the value
                auto* typePtr = context.get_builder().CreateStructGEP(ValueTy, tmp_alloca, 0);
                auto* i32_ty_tag = llvm::Type::getInt32Ty(Context);
                auto* type_tag = context.get_builder().CreateLoad(i32_ty_tag, typePtr, "type_tag");
                
                // Extract value field (field 1 contains the value as i64)
                auto* valuePtr = context.get_builder().CreateStructGEP(ValueTy, tmp_alloca, 1);
                auto* i64_ty = llvm::Type::getInt64Ty(Context);
                auto* value64 = context.get_builder().CreateLoad(i64_ty, valuePtr, "value64");
                
                // Check if it's TAG_FLOAT (2) or TAG_INT (1)
                auto* TAG_INT_const = llvm::ConstantInt::get(i32_ty_tag, 1);
                auto* TAG_FLOAT_const = llvm::ConstantInt::get(i32_ty_tag, 2);
                auto* is_int = context.get_builder().CreateICmpEQ(type_tag, TAG_INT_const, "is_int");
                auto* is_float = context.get_builder().CreateICmpEQ(type_tag, TAG_FLOAT_const, "is_float");
                
                // Create basic blocks for different extraction paths
                auto* int_block = llvm::BasicBlock::Create(Context, "extract_int", main_start);
                auto* float_block = llvm::BasicBlock::Create(Context, "extract_float", main_start);
                auto* default_block = llvm::BasicBlock::Create(Context, "extract_default", main_start);
                auto* merge_block = llvm::BasicBlock::Create(Context, "extract_merge", main_start);
                
                // Branch based on type - first check if int, then if float, else default
                auto* builder = &context.get_builder();
                auto* check_float_block = llvm::BasicBlock::Create(Context, "check_float", main_start);
                builder->CreateCondBr(is_int, int_block, check_float_block);
                
                builder->SetInsertPoint(check_float_block);
                builder->CreateCondBr(is_float, float_block, default_block);
                
                // Extract as integer (TAG_INT)
                builder->SetInsertPoint(int_block);
                auto* int_val = builder->CreateTrunc(value64, i32_ty, "int_val");
                builder->CreateBr(merge_block);
                
                // Extract as float (TAG_FLOAT) - bitcast i64 to double, then convert to i32
                builder->SetInsertPoint(float_block);
                auto* f64_ty = llvm::Type::getDoubleTy(Context);
                auto* float_val_bits = builder->CreateBitCast(value64, f64_ty, "float_bits");
                auto* float_val = builder->CreateFPToSI(float_val_bits, i32_ty, "float_val");
                builder->CreateBr(merge_block);
                
                // Default case - return 0 for other types
                builder->SetInsertPoint(default_block);
                auto* default_val = llvm::ConstantInt::get(i32_ty, 0);
                builder->CreateBr(merge_block);
                
                // Merge block - phi node to select the correct value
                builder->SetInsertPoint(merge_block);
                auto* phi = builder->CreatePHI(i32_ty, 3, "extracted_val");
                phi->addIncoming(int_val, int_block);
                phi->addIncoming(float_val, float_block);
                phi->addIncoming(default_val, default_block);
                return_value = phi;
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

// Função para executar modo REPL
int run_repl_mode() {
    using namespace narval::frontend::interactive;
    
    std::cout << "Narval REPL - Modo Interativo\n";
    std::cout << "Digite ':help' para comandos ou ':quit' para sair\n\n";
    
    // Inicializar base dir como diretório atual
    nv_base_dir_storage = std::filesystem::current_path().string();
    nv_base_dir = nv_base_dir_storage.c_str();
    
    try {
        // Criar sessão REPL usando o novo sistema
        auto repl_session = create_repl();
        
        // Configurar callbacks
        repl_session->set_output_callback([](const std::string& output) {
            std::cout << output << std::endl;
        });
        
        repl_session->set_error_callback([](const std::string& error) {
            std::cerr << "Error: " << error << std::endl;
        });
        
        // Iniciar o REPL
        repl_session->start();

        auto* repl = repl_session->get_session<Repl>();
        if (!repl) {
            std::cerr << "Erro ao obter interface do REPL" << std::endl;
            return 1;
        }

        std::string line;
        while (true) {
            std::cout << "narval> ";
            std::cout.flush();

            if (!std::getline(std::cin, line)) {
                std::cout << "\n";
                break;
            }

            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t\n\r"));
            line.erase(line.find_last_not_of(" \t\n\r") + 1);
            if (line.empty()) continue;

            // Commands
            if (line == ":quit" || line == ":exit") {
                break;
            }
            if (line == ":help") {
                std::cout << "Comandos disponíveis:\n";
                std::cout << "  :help     - Show available commands\n";
                std::cout << "  :quit     - Exit REPL\n";
                std::cout << "  :symbols  - Show defined symbols\n";
                std::cout << "  :debug    - Toggle debug mode\n";
                std::cout << "  :clear    - Clear session\n";
                continue;
            }
            if (line == ":symbols") {
                auto syms = repl->session_manager().list_symbols_valid();
                std::cout << "Symbols (" << syms.size() << "):\n";
                for (const auto& s : syms) {
                    std::cout << "  " << s << "\n";
                }
                continue;
            }
            if (line == ":clear") {
                repl->session_manager().reset();
                std::cout << "Session cleared.\n";
                continue;
            }
            if (line == ":debug") {
                repl->set_debug(!repl->debug());
                std::cout << "debug=" << (repl->debug() ? "true" : "false") << "\n";
                continue;
            }

            auto result = repl->execute_line(line);
            if (!result.ok) {
                if (!result.error.empty()) {
                    std::cerr << "Error: " << result.error << std::endl;
                }
            }

            if (!result.output.empty()) {
                std::cout << result.output << std::endl;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Erro ao inicializar REPL: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

// Função para executar modo Notebook
int run_notebook_mode() {
    using namespace narval::frontend::interactive;
    
    std::cout << "Narval Notebook - Modo Interativo\n";
    std::cout << "Digite 'help' para comandos ou 'quit' para sair\n\n";
    
    // Inicializar base dir como diretório atual
    nv_base_dir_storage = std::filesystem::current_path().string();
    nv_base_dir = nv_base_dir_storage.c_str();
    
    try {
        // Criar sessão Notebook usando o novo sistema
        auto notebook_session = create_notebook("Interactive Notebook");
        
        // Configurar callbacks
        notebook_session->set_output_callback([](const std::string& output) {
            std::cout << output << std::endl;
        });
        
        notebook_session->set_error_callback([](const std::string& error) {
            std::cerr << "Error: " << error << std::endl;
        });
        
        // Obter interface do notebook
        auto* notebook = notebook_session->get_session<Notebook>();
        if (!notebook) {
            std::cerr << "Erro ao obter interface do notebook" << std::endl;
            return 1;
        }
        
        std::cout << "Notebook criado. Comandos disponíveis:\n";
        std::cout << "  new <code>     - Criar nova célula\n";
        std::cout << "  run <cell_id>  - Executar célula\n";
        std::cout << "  list           - Listar células\n";
        std::cout << "  clear          - Limpar sessão\n";
        std::cout << "  save <file>    - Salvar notebook\n";
        std::cout << "  quit           - Sair\n\n";
        
        std::string line;
        while (true) {
            std::cout << "notebook> ";
            std::cout.flush();
            
            if (!std::getline(std::cin, line)) {
                std::cout << "\n";
                break;
            }
            
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t\n\r"));
            line.erase(line.find_last_not_of(" \t\n\r") + 1);
            
            if (line.empty()) continue;
            
            if (line == "quit" || line == "exit") {
                break;
            }
            
            if (line == "help") {
                std::cout << "Comandos disponíveis:\n";
                std::cout << "  new <code>     - Criar nova célula\n";
                std::cout << "  run <cell_id>  - Executar célula\n";
                std::cout << "  list           - Listar células\n";
                std::cout << "  clear          - Limpar sessão\n";
                std::cout << "  save <file>    - Salvar notebook\n";
                std::cout << "  quit           - Sair\n";
                continue;
            }
            
            if (line == "list") {
                auto cell_ids = notebook->get_cell_ids();
                std::cout << "Células (" << cell_ids.size() << "):\n";
                for (const auto& id : cell_ids) {
                    const auto* cell = notebook->get_cell(id);
                    if (cell) {
                        std::cout << "  " << id << " [" 
                                  << (cell->type == CellType::Code ? "Code" : "Markdown") 
                                  << "] " 
                                  << (cell->content.length() > 30 ? cell->content.substr(0, 30) + "..." : cell->content)
                                  << "\n";
                    }
                }
                continue;
            }
            
            if (line == "clear") {
                notebook->reset_session();
                std::cout << "Sessão limpa.\n";
                continue;
            }
            
            // Comando: new <code>
            if (line.substr(0, 4) == "new ") {
                std::string code = line.substr(4);
                code.erase(0, code.find_first_not_of(" \t"));
                
                if (!code.empty()) {
                    std::string cell_id = notebook->create_cell(CellType::Code, code);
                    std::cout << "Célula '" << cell_id << "' criada.\n";
                    
                    // Executar automaticamente
                    if (notebook->execute_cell(cell_id)) {
                        std::cout << "Célula executada com sucesso.\n";
                    } else {
                        std::cout << "Erro ao executar célula.\n";
                    }
                } else {
                    std::cout << "Uso: new <código>\n";
                }
                continue;
            }
            
            // Comando: run <cell_id>
            if (line.substr(0, 4) == "run ") {
                std::string cell_id = line.substr(4);
                cell_id.erase(0, cell_id.find_first_not_of(" \t"));
                
                if (notebook->execute_cell(cell_id)) {
                    std::cout << "Célula '" << cell_id << "' executada.\n";
                } else {
                    std::cout << "Erro ao executar célula '" << cell_id << "'.\n";
                }
                continue;
            }
            
            // Comando: save <file>
            if (line.substr(0, 5) == "save ") {
                std::string filename = line.substr(5);
                filename.erase(0, filename.find_first_not_of(" \t"));
                
                if (notebook->save_to_file(filename)) {
                    std::cout << "Notebook salvo em '" << filename << "'.\n";
                } else {
                    std::cout << "Erro ao salvar notebook.\n";
                }
                continue;
            }
            
            // Se não é comando, tratar como nova célula
            std::string cell_id = notebook->create_cell(CellType::Code, line);
            std::cout << "Célula '" << cell_id << "' criada.\n";
            
            if (notebook->execute_cell(cell_id)) {
                std::cout << "Célula executada com sucesso.\n";
            } else {
                std::cout << "Erro ao executar célula.\n";
            }
        }
        
        std::cout << "Saindo do Notebook...\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Erro ao inicializar Notebook: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
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
            std::cout << "  --repl, -i, -r     Iniciar REPL interativo\n";
            std::cout << "  --notebook, -n     Iniciar modo Notebook\n";
            std::cout << "  --help, -h          Mostrar esta ajuda\n";
            std::cout << "\nModos:\n";
            std::cout << "  Se nenhuma opção for fornecida e um arquivo for especificado,\n";
            std::cout << "  o compilador executa em modo batch (compilação normal).\n";
            std::cout << "  Se --repl for especificado, inicia o REPL com comandos como :help, :quit, :symbols, etc.\n";
            std::cout << "  Se --notebook for especificado, inicia o modo Notebook com células e epochs.\n";
            std::cout << "\nREPL Commands:\n";
            std::cout << "  :help     - Show available commands\n";
            std::cout << "  :quit     - Exit REPL\n";
            std::cout << "  :symbols  - Show defined symbols\n";
            std::cout << "  :session  - Show session information\n";
            std::cout << "  :debug    - Toggle debug mode\n";
            std::cout << "  :clear    - Clear session\n";
            std::cout << "  :load     - Load file\n";
            std::cout << "  :save     - Save session\n";
            std::cout << "\nNotebook Commands:\n";
            std::cout << "  help      - Show available commands\n";
            std::cout << "  quit      - Exit notebook\n";
            std::cout << "  new <code> - Create new cell\n";
            std::cout << "  run <id>  - Execute cell\n";
            std::cout << "  list      - List cells\n";
            std::cout << "  clear     - Clear session\n";
            std::cout << "  save      - Save notebook\n";
            return 0;
        } else if (arg[0] != '-') {
            // Argumento posicional (nome de arquivo)
            filename = arg;
        }
    }
    
    // Determinar modo de execução
    if (repl_mode) {
        return run_repl_mode();
    } else if (notebook_mode) {
        return run_notebook_mode();
    } else if (!filename.empty()) {
        return run_batch_mode(filename);
    } else {
        std::cerr << "Uso: narval [--repl|--notebook] [arquivo.nv]\n";
        std::cerr << "Use --help para mais informações.\n";
        return 1;
    }
}
