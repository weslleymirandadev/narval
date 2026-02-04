#include "frontend/interactive/jit_execution_engine.hpp"

#include <stdexcept>

#include <mutex>
#include <shared_mutex>
#include <string>
#include <variant>

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/FileSystem.h>

// Runtime initialization
extern "C" {
    void init_type_registry(void);
    void create_str(void*, const char*);
    void create_int(void*, int);
    void create_float(void*, double);
    void create_bool(void*, int);
    void nv_write(void*);
    void nv_write_no_nl(void*);
    void* nv_read(void);
    void ensure_value_type(void*, int);
    void* nv_get_global_value(const char*);  // Nova função para buscar valores globais por nome
}

namespace narval::frontend::interactive {

JitExecutionEngine::JitExecutionEngine() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    // Initialize Narval runtime before any JIT execution
    init_type_registry();

    auto jit_or_err = llvm::orc::LLJITBuilder().create();
    if (!jit_or_err) {
        auto err = jit_or_err.takeError();
        throw std::runtime_error("failed to create LLJIT: " + llvm::toString(std::move(err)));
    }
    jit_ = std::move(*jit_or_err);

    auto gen = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
        jit_->getDataLayout().getGlobalPrefix());
    if (!gen) {
        throw std::runtime_error("failed to attach process symbol generator");
    }
    jit_->getMainJITDylib().addGenerator(std::move(*gen));

    // Fallback: explicitly add the current executable as a dynamic library
    std::string exe_path = llvm::sys::fs::getMainExecutable(nullptr, nullptr);
    auto& dylib = jit_->getMainJITDylib();
    auto gen_or_err = llvm::orc::DynamicLibrarySearchGenerator::Load(
        exe_path.c_str(), jit_->getDataLayout().getGlobalPrefix());
    if (!gen_or_err) {
        llvm::consumeError(gen_or_err.takeError());
    } else {
        dylib.addGenerator(std::move(*gen_or_err));
    }

    // Explicitly register runtime symbols that JIT can't find
    auto& ctx = jit_->getExecutionSession();
    auto& main_dylib = jit_->getMainJITDylib();
    
    // Register key runtime functions
    struct RuntimeSymbol {
        const char* name;
        void* addr;
    };
    
    RuntimeSymbol symbols[] = {
        {"create_str", reinterpret_cast<void*>(&::create_str)},
        {"create_int", reinterpret_cast<void*>(&::create_int)},
        {"create_float", reinterpret_cast<void*>(&::create_float)},
        {"create_bool", reinterpret_cast<void*>(&::create_bool)},
        {"nv_write", reinterpret_cast<void*>(&::nv_write)},
        {"nv_write_no_nl", reinterpret_cast<void*>(&::nv_write_no_nl)},
        {"nv_read", reinterpret_cast<void*>(&::nv_read)},
        {"ensure_value_type", reinterpret_cast<void*>(&::ensure_value_type)},
        {"init_type_registry", reinterpret_cast<void*>(&::init_type_registry)},
        {nullptr, nullptr}
    };
    
    for (int i = 0; symbols[i].name; ++i) {
        if (symbols[i].addr) {
            auto jit_addr = llvm::JITEvaluatedSymbol::fromPointer(symbols[i].addr);
            llvm::orc::ExecutorAddr exec_addr(jit_addr.getAddress());
            llvm::orc::ExecutorSymbolDef exec_sym(exec_addr, llvm::JITSymbolFlags::Exported);
            llvm::orc::SymbolMap symbol_map;
            symbol_map[ctx.intern(symbols[i].name)] = exec_sym;
            auto err = main_dylib.define(llvm::orc::absoluteSymbols(std::move(symbol_map)));
            if (err) {
                llvm::consumeError(std::move(err));
            }
        }
    }
}

JitExecutionEngine::~JitExecutionEngine() = default;

llvm::orc::ResourceTrackerSP JitExecutionEngine::add_module(
    std::unique_ptr<llvm::Module> module,
    std::unique_ptr<llvm::orc::ThreadSafeContext> tsc
) {
    if (!module || !tsc) return nullptr;

    auto tracker = jit_->getMainJITDylib().createResourceTracker();
    if (auto err = jit_->addIRModule(tracker, llvm::orc::ThreadSafeModule(std::move(module), std::move(*tsc)))) {
        throw std::runtime_error("failed to add module");
    }
    return tracker;
}

void JitExecutionEngine::remove_module(const llvm::orc::ResourceTrackerSP& tracker) {
    if (!tracker) return;
    if (auto err = tracker->remove()) {
        throw std::runtime_error("failed to remove module");
    }
}

void JitExecutionEngine::execute_void_function(const std::string& name) {
    auto sym = jit_->lookup(name);
    if (!sym) {
        auto err = sym.takeError();
        throw std::runtime_error("failed to lookup entry: " + llvm::toString(std::move(err)));
    }

    using FnPtr = void (*)();
    auto addr = sym->getValue();
    auto* entry = reinterpret_cast<FnPtr>(static_cast<uintptr_t>(addr));
    try {
        entry();
    } catch (const std::exception& e) {
        throw std::runtime_error("JIT execution failed: " + std::string(e.what()));
    } catch (...) {
        throw std::runtime_error("JIT execution failed: unknown error");
    }
}

} // namespace narval::frontend::interactive
