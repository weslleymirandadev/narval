#include "frontend/interactive/repl_state.hpp"
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <filesystem>

#include <llvm/AsmParser/Parser.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Bitcode/BitcodeReader.h>

namespace nv {

REPLState::REPLState()
    : llvm_context(std::make_unique<llvm::LLVMContext>()),
      module(std::make_unique<llvm::Module>("narval_repl", *llvm_context)),
      builder(std::make_unique<llvm::IRBuilder<llvm::NoFolder>>(*llvm_context)),
      checker(std::make_unique<Checker>()) {
}

REPLState::~REPLState() = default;

bool REPLState::initialize() {
    try {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();

        auto jit_expected = llvm::orc::LLJITBuilder().create();
        if (!jit_expected) {
            llvm::Error err = jit_expected.takeError();
            std::cerr << "Failed to create JIT" << std::endl;
            llvm::consumeError(std::move(err));
            return false;
        }
        jit = std::move(*jit_expected);

        // Load project lib IR modules (e.g., read.ll) into the JIT so symbols like nv_read are available
        load_lib_modules();

        register_runtime_functions();

        checker->push_scope();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception during REPL initialization: " << e.what() << std::endl;
        return false;
    }
}

static const char* const RUNTIME_SYMBOLS[] = {
    "nv_write", "nv_write_no_nl",
    "create_int", "create_float", "create_bool", "create_str",
    "create_array", "create_vector", "create_map",
    "ensure_value_type", "nv_read",
    "string_to_upper_case", "string_replace", "string_includes",
    "vector_push_method", "vector_pop_method", "vector_get_method", "vector_set_method",
    "array_get_index_v", "array_set_index_v",
    "tuple_get_impl", "tuple_set_impl",
    "json_parse", "json_dump", "json_stringify",
    nullptr
};

void REPLState::register_runtime_functions() {
    std::string runtime_path;
    const char* narval_home = std::getenv("NARVAL_HOME");
    if (narval_home) {
        runtime_path = std::string(narval_home) + "/runtime.so";
    } else {
        std::string dev_runtime = std::string(NARVAL_SOURCE_DIR) + "/build/lib/runtime.so";
        std::ifstream check_file(dev_runtime);
        if (check_file.good()) {
            runtime_path = dev_runtime;
            std::cout << "Using development runtime from: " << runtime_path << std::endl;
        } else {
            runtime_path = "/usr/lib/narval/runtime.so";
            std::cout << "Using production runtime from: " << runtime_path << std::endl;
        }
        check_file.close();
    }

    void* runtime_handle = dlopen(runtime_path.c_str(), RTLD_LAZY);
    if (!runtime_handle) {
        std::cerr << "Failed to load runtime from " << runtime_path << ": " << dlerror() << std::endl;
        return;
    }
    std::cout << "Loaded runtime from: " << runtime_path << std::endl;

    for (const char* const* p = RUNTIME_SYMBOLS; *p; ++p) {
        const char* name = *p;
        void* ptr = dlsym(runtime_handle, name);
        if (ptr) {
            symbols[name] = static_cast<llvm::JITTargetAddress>(reinterpret_cast<uintptr_t>(ptr));
        }
    }

    // If the runtime provides `nv_read` (C symbol), also expose it to JIT
    // under the LLVM-intrinsic name `llvm.nv_read` so IR modules that call
    // the intrinsic can link to the same implementation.
    if (symbols.find("nv_read") != symbols.end() && symbols.find("llvm.nv_read") == symbols.end()) {
        symbols["llvm.nv_read"] = symbols["nv_read"];
    }

    llvm::orc::SymbolMap sym_map;
    auto& es = jit->getExecutionSession();
    for (const auto& [name, addr] : symbols) {
        sym_map[es.intern(name)] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr(addr),
            llvm::JITSymbolFlags::Exported);
    }
    if (auto err = jit->getMainJITDylib().define(llvm::orc::absoluteSymbols(std::move(sym_map)))) {
        std::cerr << "Failed to define runtime symbols in JIT" << std::endl;
        llvm::consumeError(std::move(err));
    }

    auto& dl = jit->getDataLayout();
    auto generator = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
        dl.getGlobalPrefix()
    );
    if (generator) {
        jit->getMainJITDylib().addGenerator(std::move(*generator));
    } else {
        llvm::consumeError(generator.takeError());
    }
}

void REPLState::load_lib_modules() {
    std::string lib_dir = std::string(NARVAL_SOURCE_DIR) + "/lib";
    try {
        for (const auto& entry : std::filesystem::directory_iterator(lib_dir)) {
            if (!entry.is_regular_file()) continue;
            auto path = entry.path();
            auto ext = path.extension().string();
            if (ext == ".ll" || ext == ".bc") {
                std::string file_path = path.string();
                if (ext == ".ll") {
                    // parse assembly
                    llvm::SMDiagnostic err;
                    auto ctx = std::make_unique<llvm::LLVMContext>();
                    std::string file_contents;
                    std::ifstream in(file_path);
                    if (!in) continue;
                    file_contents.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    std::unique_ptr<llvm::Module> mod = llvm::parseAssemblyString(file_contents, err, *ctx);
                    if (!mod) continue;
                    auto tsm = llvm::orc::ThreadSafeModule(std::move(mod), std::move(ctx));
                    if (auto err2 = jit->addIRModule(std::move(tsm))) {
                        llvm::consumeError(std::move(err2));
                    }
                } else {
                    // .bc bitcode
                    // Read bitcode file into a MemoryBuffer using ErrorOr API
                    auto mb_or_err = llvm::MemoryBuffer::getFile(file_path);
                    if (!mb_or_err) continue;
                    auto ctx = std::make_unique<llvm::LLVMContext>();
                    std::unique_ptr<llvm::MemoryBuffer> mem_buf = std::move(*mb_or_err);
                    llvm::Expected<std::unique_ptr<llvm::Module>> m_or_err = llvm::parseBitcodeFile(mem_buf->getMemBufferRef(), *ctx);
                    if (!m_or_err) continue;
                    auto mod = std::move(*m_or_err);
                    auto tsm = llvm::orc::ThreadSafeModule(std::move(mod), std::move(ctx));
                    if (auto err2 = jit->addIRModule(std::move(tsm))) {
                        llvm::consumeError(std::move(err2));
                    }
                }
            }
        }
    } catch (...) {
        // ignore errors when loading optional lib modules
    }
}

void REPLState::reset() {
    symbols.clear();
    repl_global_names.clear();
    repl_var_values.clear();
    repl_globals_added.clear();
    result_counter = 0;

    llvm_context = std::make_unique<llvm::LLVMContext>();
    module = std::make_unique<llvm::Module>("narval_repl", *llvm_context);
    builder = std::make_unique<llvm::IRBuilder<llvm::NoFolder>>(*llvm_context);

    checker = std::make_unique<Checker>();
    checker->push_scope();

    auto jit_expected = llvm::orc::LLJITBuilder().create();
    if (jit_expected) {
        jit = std::move(*jit_expected);
        register_runtime_functions();
        load_lib_modules();
    }
}

bool REPLState::add_symbol(const std::string& name, llvm::JITTargetAddress addr) {
    symbols[name] = addr;
    return true;
}

std::optional<llvm::JITTargetAddress> REPLState::get_symbol(const std::string& name) {
    auto it = symbols.find(name);
    if (it != symbols.end()) return it->second;
    if (jit) {
        if (auto symbol = jit->lookup(name)) return symbol->getValue();
    }
    return std::nullopt;
}

} // namespace nv
