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
#include <llvm/IR/InlineAsm.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/IR/DIBuilder.h>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cctype>
#include <cstdlib>

extern "C" const char* nv_base_dir = nullptr; // visible to C runtime
static std::string nv_base_dir_storage;
static std::string nv_executable_path_storage;

struct BuildTarget {
    std::string name;
    std::string triple;
    std::string cpu;
    bool link_with_host_toolchain;
};

static std::string normalize_target_name(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::replace(name.begin(), name.end(), '_', '-');
    return name;
}

static std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

static std::vector<std::string> split_target_parts(const std::string& value) {
    std::vector<std::string> parts;
    std::string current;

    for (char ch : value) {
        if (ch == '-') {
            parts.push_back(current);
            current.clear();
        } else {
            current += ch;
        }
    }
    parts.push_back(current);
    return parts;
}

static std::string canonical_arch(std::string arch) {
    arch = normalize_target_name(std::move(arch));
    if (arch == "x64" || arch == "amd64" || arch == "x86-64") return "x86_64";
    if (arch == "x86" || arch == "i386" || arch == "i486" || arch == "i586" || arch == "i686") return "i686";
    if (arch == "arm64") return "aarch64";
    return arch;
}

static std::string canonical_os(std::string os) {
    os = normalize_target_name(std::move(os));
    if (os == "windows" || os == "win32" || os == "win64" || os == "win") return "windows";
    if (os == "macos" || os == "macosx" || os == "osx") return "darwin";
    if (os == "none" || os == "baremetal" || os == "bare-metal") return "elf";
    return os;
}

static std::string default_vendor_for_os(const std::string& os) {
    if (os == "windows") return "pc";
    if (os == "darwin" || os == "ios" || os == "tvos" || os == "watchos") return "apple";
    return "unknown";
}

static std::string default_env_for_os(const std::string& os) {
    if (os == "windows") return "msvc";
    if (os == "linux") return "gnu";
    return "";
}

static std::string canonicalize_requested_triple(const std::string& requested_target) {
    std::string normalized = lowercase(requested_target);
    std::vector<std::string> parts = split_target_parts(normalized);
    if (parts.empty()) return requested_target;

    parts[0] = canonical_arch(parts[0]);

    // Short forms:
    //   x86_64-windows       -> x86_64-pc-windows-msvc
    //   aarch64-linux        -> aarch64-unknown-linux-gnu
    //   riscv64-elf          -> riscv64-unknown-elf
    //   x86_64-windows-gnu   -> x86_64-pc-windows-gnu
    if (parts.size() == 2) {
        const std::string os = canonical_os(parts[1]);
        const std::string env = default_env_for_os(os);
        if (env.empty()) {
            return parts[0] + "-" + default_vendor_for_os(os) + "-" + os;
        }
        return parts[0] + "-" + default_vendor_for_os(os) + "-" + os + "-" + env;
    }

    if (parts.size() == 3) {
        const std::string second = canonical_os(parts[1]);
        const std::string third = canonical_os(parts[2]);

        // arch-os-env shorthand, especially x86_64-windows-gnu.
        if (second == "windows" || second == "linux" || second == "darwin" || second == "elf") {
            return parts[0] + "-" + default_vendor_for_os(second) + "-" + second + "-" + parts[2];
        }

        // arch-vendor-os, fill env where the platform normally needs one.
        const std::string env = default_env_for_os(third);
        if (env.empty()) {
            return parts[0] + "-" + parts[1] + "-" + third;
        }
        return parts[0] + "-" + parts[1] + "-" + third + "-" + env;
    }

    return llvm::Triple::normalize(requested_target);
}

static const std::map<std::string, BuildTarget>& get_build_targets() {
    static const std::map<std::string, BuildTarget> targets = {
        {"native",      {"native",      llvm::sys::getDefaultTargetTriple(), "generic", true}},
        {"host",        {"native",      llvm::sys::getDefaultTargetTriple(), "generic", true}},
        {"x86-64",      {"x86-64",      "x86_64-unknown-linux-gnu",          "generic", false}},
        {"x64",         {"x86-64",      "x86_64-unknown-linux-gnu",          "generic", false}},
        {"amd64",       {"x86-64",      "x86_64-unknown-linux-gnu",          "generic", false}},
        {"x86",         {"x86",         "i686-unknown-linux-gnu",            "generic", false}},
        {"i386",        {"x86",         "i386-unknown-linux-gnu",            "generic", false}},
        {"i686",        {"x86",         "i686-unknown-linux-gnu",            "generic", false}},
        {"aarch64",     {"aarch64",     "aarch64-unknown-linux-gnu",         "generic", false}},
        {"arm64",       {"aarch64",     "aarch64-unknown-linux-gnu",         "generic", false}},
        {"arm",         {"arm",         "armv7-unknown-linux-gnueabihf",     "generic", false}},
        {"armv7",       {"arm",         "armv7-unknown-linux-gnueabihf",     "generic", false}},
        {"riscv64",     {"riscv64",     "riscv64-unknown-elf",              "generic-rv64", false}},
        {"riscv32",     {"riscv32",     "riscv32-unknown-elf",              "generic-rv32", false}},
        {"xtensa",      {"xtensa",      "xtensa-unknown-elf",               "generic", false}},
        {"wasm32",      {"wasm32",      "wasm32-unknown-unknown",           "generic", false}},
        {"wasm64",      {"wasm64",      "wasm64-unknown-unknown",           "generic", false}},
        {"mips",        {"mips",        "mips-unknown-linux-gnu",           "generic", false}},
        {"mipsel",      {"mipsel",      "mipsel-unknown-linux-gnu",         "generic", false}},
        {"mips64",      {"mips64",      "mips64-unknown-linux-gnuabi64",    "generic", false}},
        {"powerpc",     {"powerpc",     "powerpc-unknown-linux-gnu",        "generic", false}},
        {"ppc",         {"powerpc",     "powerpc-unknown-linux-gnu",        "generic", false}},
        {"powerpc64",   {"powerpc64",   "powerpc64-unknown-linux-gnu",      "generic", false}},
        {"ppc64",       {"powerpc64",   "powerpc64-unknown-linux-gnu",      "generic", false}},
        {"s390x",       {"s390x",       "s390x-unknown-linux-gnu",          "generic", false}},
        {"sparc",       {"sparc",       "sparc-unknown-linux-gnu",          "generic", false}},
        {"sparcv9",     {"sparcv9",     "sparcv9-unknown-linux-gnu",        "generic", false}},
        {"avr",         {"avr",         "avr-unknown-unknown",              "generic", false}},
        {"msp430",      {"msp430",      "msp430-unknown-unknown",           "generic", false}},
        {"nvptx",       {"nvptx",       "nvptx64-nvidia-cuda",             "generic", false}},
        {"nvptx64",     {"nvptx",       "nvptx64-nvidia-cuda",             "generic", false}},
        {"amdgcn",      {"amdgcn",      "amdgcn-amd-amdhsa",               "generic", false}},
        {"loongarch64", {"loongarch64", "loongarch64-unknown-linux-gnu",    "generic", false}},
        {"bpf",         {"bpf",         "bpf-unknown-unknown",              "generic", false}},
        {"bpfel",       {"bpfel",       "bpfel-unknown-unknown",            "generic", false}},
        {"bpfeb",       {"bpfeb",       "bpfeb-unknown-unknown",            "generic", false}},
        {"hexagon",     {"hexagon",     "hexagon-unknown-elf",              "generic", false}},
    };
    return targets;
}

static BuildTarget resolve_build_target(const std::string& requested_target) {
    const std::string key = normalize_target_name(requested_target.empty() ? "native" : requested_target);
    const auto& targets = get_build_targets();
    auto it = targets.find(key);
    if (it != targets.end()) {
        return it->second;
    }

    // Permite passar triple LLVM ou partial triple:
    // --build=riscv64-unknown-elf, --build=x86_64-windows, --build=aarch64-linux.
    if (key.find('-') != std::string::npos) {
        return {key, canonicalize_requested_triple(requested_target), "generic", false};
    }

    std::ostringstream out;
    out << "Target desconhecido '" << requested_target << "'. Targets conhecidos:";
    std::set<std::string> names;
    for (const auto& [name, target] : targets) {
        names.insert(target.name);
    }
    for (const auto& name : names) {
        out << " " << name;
    }
    throw std::runtime_error(out.str());
}

static bool is_x86_64_target(const llvm::Triple& triple) {
    return triple.getArch() == llvm::Triple::x86_64;
}

static void initialize_llvm_targets_once() {
    static bool targets_initialized = false;
    if (!targets_initialized) {
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmPrinters();
        llvm::InitializeAllAsmParsers();
        targets_initialized = true;
    }
}

static bool has_llvm_backend_for_triple(const std::string& triple, std::string& error) {
    initialize_llvm_targets_once();
    llvm::Triple llvm_triple(triple);
    return llvm::TargetRegistry::lookupTarget("", llvm_triple, error) != nullptr;
}

static std::string known_build_target_names() {
    std::set<std::string> names;
    for (const auto& [alias, target] : get_build_targets()) {
        names.insert(target.name);
    }

    std::ostringstream out;
    bool first = true;
    for (const auto& name : names) {
        if (!first) out << ", ";
        out << name;
        first = false;
    }
    return out.str();
}

static const std::map<std::string, std::vector<std::string>>& get_build_target_example_triples() {
    static const std::map<std::string, std::vector<std::string>> examples = {
        {"x86-64", {
            "x86_64-unknown-linux-gnu",
            "x86_64-pc-windows-msvc",
            "x86_64-pc-windows-gnu",
            "x86_64-apple-darwin",
            "x86_64-unknown-freebsd",
            "x86_64-unknown-elf",
        }},
        {"x86", {
            "i686-unknown-linux-gnu",
            "i686-pc-windows-msvc",
            "i686-pc-windows-gnu",
            "i686-unknown-elf",
        }},
        {"aarch64", {
            "aarch64-unknown-linux-gnu",
            "aarch64-pc-windows-msvc",
            "aarch64-apple-darwin",
            "aarch64-unknown-elf",
        }},
        {"arm", {
            "armv7-unknown-linux-gnueabihf",
            "armv7-none-eabi",
            "thumbv7em-none-eabi",
        }},
        {"riscv64", {
            "riscv64-unknown-linux-gnu",
            "riscv64-unknown-elf",
        }},
        {"riscv32", {
            "riscv32-unknown-elf",
        }},
        {"xtensa", {
            "xtensa-unknown-elf",
        }},
        {"wasm32", {
            "wasm32-unknown-unknown",
            "wasm32-wasi",
        }},
        {"wasm64", {
            "wasm64-unknown-unknown",
        }},
    };
    return examples;
}

static int print_enabled_targets() {
    initialize_llvm_targets_once();

    std::map<std::string, BuildTarget> unique_targets;
    for (const auto& [alias, target] : get_build_targets()) {
        if (unique_targets.find(target.name) == unique_targets.end() || alias == target.name) {
            unique_targets[target.name] = target;
        }
    }

    std::cout << "LLVM default triple: " << llvm::sys::getDefaultTargetTriple() << "\n";
    std::cout << "Build targets supported by this LLVM:\n";

    bool any_enabled = false;
    for (const auto& [name, target] : unique_targets) {
        std::string error;
        if (!has_llvm_backend_for_triple(target.triple, error)) {
            continue;
        }

        any_enabled = true;
        std::cout << "  " << name << " -> " << target.triple << "\n";

        auto examples_it = get_build_target_example_triples().find(name);
        if (examples_it != get_build_target_example_triples().end()) {
            for (const auto& triple : examples_it->second) {
                std::string example_error;
                if (has_llvm_backend_for_triple(triple, example_error)) {
                    std::cout << "      " << triple << "\n";
                }
            }
        }
    }

    if (!any_enabled) {
        std::cout << "  nenhum target conhecido habilitado\n";
    }

    std::cout << "\nKnown targets not enabled in this LLVM:\n";
    bool any_disabled = false;
    for (const auto& [name, target] : unique_targets) {
        std::string error;
        if (has_llvm_backend_for_triple(target.triple, error)) {
            continue;
        }

        any_disabled = true;
        std::cout << "  " << name << " -> " << target.triple << "\n";
    }

    if (!any_disabled) {
        std::cout << "  nenhum\n";
    }

    std::cout << "\nCustom triples are also accepted by --build=<triple> when this LLVM has the backend.\n";
    return 0;
}

// Função para executar modo batch (compilação normal)
// build_only=true  → compila e linka para binário nomeado; não executa
// object_only=true → compila apenas para .o nomeado; não linka nem executa
// extra_libs       → flags extras passadas ao linker (ex: "./libcpp.so")
int run_batch_mode(const std::string& filename, bool build_only = false,
                   bool object_only = false, const std::string& extra_libs = "",
                   const std::string& requested_target = "native") {
    // Use the file stem as the initial module name (consistent with Lexer)
    std::string module_name = std::filesystem::path(filename).stem().string();
    BuildTarget build_target;
    try {
        build_target = resolve_build_target(requested_target);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    llvm::Triple target_triple(build_target.triple);

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
        context.set_source_file(filename);

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

        // x86 only: alinhar RSP a 16 bytes no entry point.
        // movaps dentro de variadic fns (printf) exige RSP % 16 == 0.
        // Em aarch64 o ABI já garante alinhamento — não precisa de asm.
#if defined(__x86_64__) || defined(_M_X64)
        if (is_x86_64_target(target_triple)) {
            auto* AsmTy = llvm::FunctionType::get(llvm::Type::getVoidTy(Context), false);
            auto* Asm   = llvm::InlineAsm::get(
                AsmTy,
                "and $$-16, %rsp",
                "~{rsp},~{dirflag},~{fpsr},~{flags}",
                /*hasSideEffects=*/true
            );
            context.get_builder().CreateCall(Asm, {});
        }
#endif

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

        initialize_llvm_targets_once();

        Mod.setTargetTriple(target_triple);

        std::string error;
        const llvm::Target* target = llvm::TargetRegistry::lookupTarget("", target_triple, error);
        if (!target) {
            llvm::errs() << "Erro de target '" << build_target.name << "' ("
                         << build_target.triple << "): " << error << "\n";
            llvm::errs() << "Este LLVM precisa ter o backend correspondente habilitado.\n";
            return 1;
        }

        llvm::TargetOptions opt;
        std::unique_ptr<llvm::TargetMachine> target_machine(
            target->createTargetMachine(target_triple, build_target.cpu, "", opt, llvm::Reloc::PIC_)
        );
        if (!target_machine) {
            llvm::errs() << "Falha ao criar TargetMachine para " << build_target.triple << "\n";
            return 1;
        }

        Mod.setDataLayout(target_machine->createDataLayout());

        std::string stem = std::filesystem::path(filename).stem().string();
        // Arquivo .o: nomeado definitivamente em --object, temporário nos outros modos
        const bool cross_build = build_target.triple != llvm::sys::getDefaultTargetTriple();
        std::string obj_path;
        if (cross_build) {
            obj_path = stem + "-" + build_target.name + ".o";
        } else if (object_only) {
            obj_path = stem + ".o";
        } else {
            obj_path = "narval_tmp_" + stem + ".o";
        }

        std::error_code EC;
        llvm::raw_fd_ostream dest(obj_path, EC, llvm::sys::fs::OF_None);
        if (EC) {
            llvm::errs() << "Falha ao abrir .o: " << EC.message() << "\n";
            return 1;
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

        // Modo --object e cross-target: apenas gera o .o, sem linkar com runtime nativo.
        if (object_only || cross_build) {
            if (cross_build && build_only) {
                std::cout << "Objeto gerado para " << build_target.name
                          << " (" << build_target.triple << "): " << obj_path << "\n";
            }
            return 0;
        }

        // Resolver caminho do runtime
        std::string runtime_path;
        const char* narval_home = std::getenv("NARVAL_HOME");
        if (narval_home) {
            runtime_path = std::string(narval_home) + "/runtime.o";
        } else {
            const std::string local_runtime = std::string(NARVAL_SOURCE_DIR) + "/build/lib/runtime.o";
            if (std::filesystem::exists(local_runtime)) {
                runtime_path = local_runtime;
            } else if (!nv_executable_path_storage.empty()) {
                auto installed_runtime = std::filesystem::path(nv_executable_path_storage)
                    .parent_path()
                    .parent_path()
                    / "lib"
                    / "narval"
                    / "runtime.o";
                if (std::filesystem::exists(installed_runtime)) {
                    runtime_path = installed_runtime.string();
                } else {
                    runtime_path = std::string(NARVAL_INSTALL_RUNTIME_DIR) + "/runtime.o";
                }
            } else {
                runtime_path = std::string(NARVAL_INSTALL_RUNTIME_DIR) + "/runtime.o";
            }
        }

        // Nome do binário: o stem do fonte em --build, temporário em modo run
        std::string bin_path = build_only ? stem : ("narval_tmp_" + stem);

        // aarch64 (Termux/Android) exige PIE; x86-64 usa -no-pie para evitar
        // conflito com entry point customizado main.start sem CRT.
#if defined(__aarch64__) || defined(_M_ARM64)
        const char* pie_flag = "-pie";
#else
        const char* pie_flag = "-no-pie";
#endif
        // Bibliotecas extras: itens gerados pelo codegen (bridges Python, etc.) +
        // flag CLI (-L) + variável NARVAL_LINK_EXTRA
        std::string link_extra = extra_libs;
        if (link_extra.empty()) {
            const char* env_extra = std::getenv("NARVAL_LINK_EXTRA");
            if (env_extra) link_extra = env_extra;
        }
        // Adicionar itens gerados durante codegen (ex: narval_py_bridge_X.o)
        for (const auto& item : context.get_extra_link_items())
            link_extra += " " + item;

        const auto& ft = nv::get_feature_tracker();
        std::string link_cmd =
            std::string("gcc -g ") + runtime_path + " " +
            obj_path + " -pthread -ldl -lm -o " + bin_path + " " +
            "-Wl,-e,main.start " +
            "-nostartfiles " +
            std::string(pie_flag) + " " +
            "-lc -w " +
            "-Wl,--gc-sections " +
            (ft.strip ? "-Wl,--strip-all " : "") +
            (ft.lto   ? "-flto "           : "") +
            link_extra;

        if (system(link_cmd.c_str()) != 0) {
            std::filesystem::remove(obj_path);
            llvm::errs() << "Falha na linkedição\n";
            return 1;
        }

        // Limpar o .o do programa principal
        std::filesystem::remove(obj_path);

        // Limpar .o temporários gerados pelo codegen (bridges Python, etc.)
        for (const auto& item : context.get_extra_link_items()) {
            // Remover apenas arquivos .o gerados por nós (nome começa com narval_py_bridge_)
            if (item.size() > 2 && item.substr(item.size() - 2) == ".o" &&
                item.find("narval_py_bridge_") != std::string::npos) {
                std::filesystem::remove(item);
            }
        }

        // Modo --build: apenas gera o binário, não executa
        if (build_only) return 0;

    } catch (const std::exception& e) {
        std::cerr << "Erro durante compilação: " << e.what() << "\n";
        return 1;
    }

    std::string stem = std::filesystem::path(filename).stem().string();
    std::string bin_path = "narval_tmp_" + stem;
    int exit_code = system(("./" + bin_path).c_str());
    std::filesystem::remove(bin_path);
    return exit_code;
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
        config.prompt = ">>> ";
        config.multiline_prompt = "... ";
        config.output_prompt = "<<< ";
        config.label_write_output = true;
        
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

int run_notebook_mode() {
    try {
        nv::REPLConfig cfg;
        cfg.enable_readline = true;
        cfg.show_banner = false;
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
    if (argc > 0 && argv[0]) {
        nv_executable_path_storage = std::filesystem::absolute(argv[0]).lexically_normal().string();
    }

    // Inicializar o sistema de tipos do runtime antes de tudo
    extern void register_global_init(void);
    register_global_init();
    
    // Parse argumentos de linha de comando
    bool repl_mode = false;
    bool notebook_mode = false;
    bool build_only = false;
    bool object_only = false;
    std::string filename;
    std::string extra_libs;
    std::string build_target = "native";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--repl" || arg == "-i" || arg == "-r") {
            repl_mode = true;
        } else if (arg == "--notebook" || arg == "-n") {
            notebook_mode = true;
        } else if (arg == "--enabled-targets") {
            return print_enabled_targets();
        } else if (arg == "--build" || arg == "-b") {
            build_only = true;
            build_target = "native";
        } else if (arg.rfind("--build=", 0) == 0) {
            build_only = true;
            build_target = arg.substr(std::string("--build=").size());
        } else if (arg == "--object" || arg == "-c") {
            object_only = true;
        } else if ((arg == "-L" || arg == "--link") && i + 1 < argc) {
            extra_libs += std::string(argv[++i]) + " ";
        } else if (arg.substr(0, 2) == "-L" && arg.size() > 2) {
            extra_libs += arg.substr(2) + " ";
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Uso: narval [opções] [arquivo.nv]\n";
            std::cout << "\nOpções:\n";
            std::cout << "  --repl, -i, -r     Iniciar REPL interativo\n";
            std::cout << "  --notebook, -n     Iniciar Notebook interativo\n";
            std::cout << "  --build, -b        Compilar para o target atual sem executar\n";
            std::cout << "  --build=<target>   Compilar para outro target LLVM (ex: --build=xtensa)\n";
            std::cout << "  --enabled-targets  Listar targets/triples habilitados neste LLVM\n";
            std::cout << "  --object, -c       Compilar para .o sem linkar\n";
            std::cout << "  -L <lib>           Linkar biblioteca extra (ex: ./libfoo.so)\n";
            std::cout << "  --help, -h         Mostrar esta ajuda\n";
            std::cout << "\nExemplos:\n";
            std::cout << "  narval              # abre o REPL\n";
            std::cout << "  narval prog.nv      # compila e executa\n";
            std::cout << "  narval --build prog.nv          # gera binário ./prog\n";
            std::cout << "  narval --build=xtensa prog.nv   # gera prog-xtensa.o\n";
            std::cout << "  narval --object prog.nv  # gera prog.o\n";
            std::cout << "\nTargets conhecidos:\n";
            std::cout << "  " << known_build_target_names() << "\n";
            return 0;
        } else if (arg[0] != '-') {
            filename = arg;
        }
    }

    // Determinar modo de execução
    if (notebook_mode) {
        return run_notebook_mode();
    } else if (repl_mode) {
        return run_repl_mode();
    } else if (!filename.empty()) {
        return run_batch_mode(filename, build_only, object_only, extra_libs, build_target);
    } else {
        // Sem argumentos: entrar no REPL
        return run_repl_mode();
    }
}
