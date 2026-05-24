#include <iostream>
#include <fstream>
#include <string>
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/module_manager.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/attributes/attribute_mapper.hpp"
#include "backend/nir/NIRGenerationContext.hpp"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "llvm/Support/Error.h"

// Implemented REPL system
#include "frontend/interactive/repl.hpp"

// Notebook mode (Jupyter-like)
#include "frontend/interactive/notebook.hpp"

// Runtime initialization
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

    // Allow passing an LLVM triple or partial triple:
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

// Run batch mode (normal compilation)
// build_only=true  -> compile and link to a named binary; do not run
// object_only=true -> compile only to a named .o; do not link or run
// extra_libs       -> extra flags passed to the linker (ex: "./libcpp.so")
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
        nv::reset_feature_tracker();
        nv::CompilationAttributes attrs = nv::map_compilation_attributes(ast.get());
        const bool no_std = attrs.no_std;

        // Determinar entry point quando @[no_std]:
        //   1. naked_asm def _start / main  — controle total sem wrapper
        //   2. def _start / def main        — função regular, sem wrapper
        //   3. nenhum                       — erro explícito antes de tentar linkar
        std::string no_std_entry; // símbolo real a passar ao linker (-Wl,-e,X)
        if (no_std) {
            const std::string naked = nv::program_naked_asm_entry(ast.get());
            if (!naked.empty()) {
                no_std_entry = naked;
            } else if (nv::program_has_function(ast.get(), "_start")) {
                no_std_entry = "_start";
            } else if (nv::program_has_function(ast.get(), "main")) {
                no_std_entry = "main";
            } else {
                llvm::errs() << "error: @[no_std] requer um entry point\n"
                             << "  defina 'naked_asm def _start' ou 'def _start' / 'def main'\n";
                return 1;
            }
        }

        // Create checker for type inference
        nv::Checker checker;
        checker.apply_compilation_attributes(attrs);
        checker.set_source_file(filename);
        // Check types before code generation
        if (ast) {
            checker.check_node(ast.get());
        }

        // Abort if semantic errors were found
        if (checker.err) {
            return 1;
        }

        mlir::MLIRContext mlir_ctx;
        nv::NIRGenerationContext nir_ctx(mlir_ctx, filename);
        nir_ctx.set_type_checker(&checker);

        // Build main.start as the program entry point
        auto& b  = nir_ctx.get_builder();
        auto  ul = b.getUnknownLoc();
        auto  void_fn_ty = mlir::FunctionType::get(&mlir_ctx, {}, {});
        auto  main_fn = mlir::func::FuncOp::create(b, ul, "main.start", void_fn_ty);
        main_fn.setPublic();
        auto* entry_blk = main_fn.addEntryBlock();
        b.setInsertionPointToStart(entry_blk);
        nir_ctx.set_current_func(main_fn);

        if (!no_std) {
            auto reg_fn = nir_ctx.ensure_runtime_func("register_global_init", void_fn_ty);
            mlir::func::CallOp::create(b, ul, reg_fn, mlir::ValueRange{});
        }

        nv::generate_ir_nir(std::move(ast), nir_ctx);

        // Finalize main.start: add _exit(0) and a return terminator
        if (!no_std) {
            auto i32_ty = b.getI32Type();
            auto exit_fn_ty = mlir::FunctionType::get(&mlir_ctx, {i32_ty}, {});
            auto exit_fn = nir_ctx.ensure_runtime_func("_exit", exit_fn_ty);
            auto zero = mlir::arith::ConstantIntOp::create(b, ul, 0, 32);
            mlir::func::CallOp::create(b, ul, exit_fn, mlir::ValueRange{zero.getResult()});
        }
        if (entry_blk->empty() ||
            !entry_blk->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
            mlir::func::ReturnOp::create(b, ul);
        }
        // Lower NIR -> LLVM IR
        llvm::LLVMContext nir_llvm_ctx;
        auto mod_or_err = nir_ctx.lower_to_llvm_ir(nir_llvm_ctx);
        if (!mod_or_err) {
            llvm::errs() << "NIR lowering error: "
                         << llvm::toString(mod_or_err.takeError()) << "\n";
            return 1;
        }
        auto& nir_mod = **mod_or_err;

        initialize_llvm_targets_once();
        nir_mod.setTargetTriple(target_triple);

        std::string nir_err;
        const llvm::Target* nir_target =
            llvm::TargetRegistry::lookupTarget("", target_triple, nir_err);
        if (!nir_target) {
            llvm::errs() << "NIR: target error: " << nir_err << "\n";
            return 1;
        }
        llvm::TargetOptions nir_opt;
        std::unique_ptr<llvm::TargetMachine> nir_tm(
            nir_target->createTargetMachine(
                target_triple, build_target.cpu, "", nir_opt, llvm::Reloc::PIC_));
        nir_mod.setDataLayout(nir_tm->createDataLayout());

        std::string stem = std::filesystem::path(filename).stem().string();
        std::string obj_path = object_only ? stem + ".o"
                                           : "narval_nir_tmp_" + stem + ".o";

        std::error_code nir_ec;
        llvm::raw_fd_ostream dest(obj_path, nir_ec, llvm::sys::fs::OF_None);
        if (nir_ec) {
            llvm::errs() << "NIR: cannot open .o: " << nir_ec.message() << "\n";
            return 1;
        }
        if (llvm::verifyModule(nir_mod, &llvm::errs())) {
            nir_mod.print(llvm::errs(), nullptr);
            llvm::errs() << "NIR module verification failed\n";
            return 1;
        }
        llvm::legacy::PassManager nir_pm;
        if (nir_tm->addPassesToEmitFile(
                nir_pm, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
            llvm::errs() << "NIR: cannot emit object file\n";
            return 1;
        }
        nir_pm.run(nir_mod);
        dest.flush();

        if (object_only) return 0;

        // Resolve runtime path
        std::string nir_runtime_path, nir_runtime_nostd_path;
        const char* narval_home_nir = std::getenv("NARVAL_HOME");
        if (narval_home_nir) {
            nir_runtime_path       = std::string(narval_home_nir) + "/runtime.o";
            nir_runtime_nostd_path = std::string(narval_home_nir) + "/runtime_nostd.o";
        } else {
            const std::string lr = std::string(NARVAL_SOURCE_DIR) + "/build/lib/runtime.o";
            const std::string ln = std::string(NARVAL_SOURCE_DIR) + "/build/lib/runtime_nostd.o";
            nir_runtime_path       = std::filesystem::exists(lr) ? lr
                : std::string(NARVAL_INSTALL_RUNTIME_DIR) + "/runtime.o";
            nir_runtime_nostd_path = std::filesystem::exists(ln) ? ln
                : std::string(NARVAL_INSTALL_RUNTIME_DIR) + "/runtime_nostd.o";
        }

        std::string bin_path = build_only ? stem : ("narval_nir_tmp_" + stem);
#if defined(__aarch64__) || defined(_M_ARM64)
        const char* nir_pie = "-pie";
#else
        const char* nir_pie = "-no-pie";
#endif
        std::string nir_link_extra = extra_libs;
        if (nir_link_extra.empty()) {
            const char* env_x = std::getenv("NARVAL_LINK_EXTRA");
            if (env_x) nir_link_extra = env_x;
        }
        for (const auto& item : nir_ctx.get_extra_link_items())
            nir_link_extra += " " + item;

        const auto& ft = nv::get_feature_tracker();
        std::string nir_link_cmd;
        if (no_std) {
            std::string nostd_rt = std::filesystem::exists(nir_runtime_nostd_path)
                ? nir_runtime_nostd_path + " " : "";
            nir_link_cmd =
                std::string("gcc ") + obj_path + " " + nostd_rt + "-o " + bin_path +
                " -nostdlib -nostartfiles " + nir_pie +
                " -Wl,-e," + no_std_entry +
                " -Wl,--gc-sections " +
                (ft.strip ? "-Wl,--strip-all " : "") +
                (ft.lto   ? "-flto "           : "") +
                nir_link_extra;
        } else {
            nir_link_cmd =
                std::string("gcc ") + nir_runtime_path + " " +
                obj_path + " -pthread -ldl -lm -o " + bin_path +
                " -Wl,-e,_narval_entry -nostartfiles " + nir_pie +
                " -lc -w -Wl,--gc-sections " +
                (ft.strip ? "-Wl,--strip-all " : "") +
                (ft.lto   ? "-flto "           : "") +
                nir_link_extra;
        }
        if (system(nir_link_cmd.c_str()) != 0) {
            std::filesystem::remove(obj_path);
            llvm::errs() << "NIR: link failed\n";
            return 1;
        }
        std::filesystem::remove(obj_path);

        if (build_only) return 0;

        // Run the produced binary
        int nir_exit = system(("./" + bin_path).c_str());
        std::filesystem::remove(bin_path);
        return nir_exit;
    } catch (const std::exception& e) {
        std::cerr << "Error during compilation: " << e.what() << "\n";
        return 1;
    }
}

int run_repl_mode() {
    try {
        nv_base_dir_storage = std::filesystem::current_path().string();
        nv_base_dir = nv_base_dir_storage.c_str();
        
        nv::REPLConfig config;
        config.enable_readline = true;
        config.show_prompt = true;
        config.show_errors = true;
        config.show_warnings = true;
        config.prompt = ">>> ";
        config.multiline_prompt = "... ";
        config.output_prompt = "<<< ";
        config.label_write_output = true;
        
        auto repl = std::make_unique<nv::REPL>(config);
        
        if (!repl->initialize()) {
            std::cerr << "Failed to initialize REPL" << std::endl;
            return 1;
        }
        
        repl->run();
        
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error initializing REPL: " << e.what() << std::endl;
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
        std::cerr << "Error initializing Notebook: " << e.what() << std::endl;
        return 1;
    }
}

int main(int argc, char* argv[]) {
    if (argc > 0 && argv[0]) {
        nv_executable_path_storage = std::filesystem::absolute(argv[0]).lexically_normal().string();
    }

    extern void register_global_init(void);
    register_global_init();
    
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
            std::cout << "Usage: narval [options] [file.nv]\n";
            std::cout << "\nOptions:\n";
            std::cout << "  --repl, -i, -r     Start the interactive REPL\n";
            std::cout << "  --notebook, -n     Start the interactive notebook\n";
            std::cout << "  --build, -b        Compile for the current target without running\n";
            std::cout << "  --build=<target>   Compile for another LLVM target (ex: --build=xtensa)\n";
            std::cout << "  --enabled-targets  List LLVM targets/triples enabled in this build\n";
            std::cout << "  --object, -c       Compile to .o without linking\n";
            std::cout << "  -L <lib>           Link an extra library (ex: ./libfoo.so)\n";
            std::cout << "  --help, -h         Show this help\n";
            std::cout << "\nExamples:\n";
            std::cout << "  narval              # open the REPL\n";
            std::cout << "  narval prog.nv      # compile and run\n";
            std::cout << "  narval --build prog.nv          # generate ./prog\n";
            std::cout << "  narval --build=xtensa prog.nv   # generate prog-xtensa.o\n";
            std::cout << "  narval --object prog.nv  # generate prog.o\n";
            std::cout << "\nKnown targets:\n";
            std::cout << "  " << known_build_target_names() << "\n";
            return 0;
        } else if (arg[0] != '-') {
            filename = arg;
        }
    }

    if (notebook_mode) {
        return run_notebook_mode();
    } else if (repl_mode) {
        return run_repl_mode();
    } else if (!filename.empty()) {
        return run_batch_mode(filename, build_only, object_only, extra_libs, build_target);
    } else {
        return run_repl_mode();
    }
}
