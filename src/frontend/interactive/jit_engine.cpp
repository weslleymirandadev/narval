#include "frontend/interactive/jit_engine.hpp"
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/Error.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Module.h>
#include <iostream>

namespace nv {
namespace interactive {

JITEngine::JITEngine() {
    // Inicializar targets LLVM
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    // Nota: Não precisamos de um LLVMContext próprio aqui,
    // pois o contexto será gerenciado pelos módulos ThreadSafe
}

JITEngine::~JITEngine() {
    // Cleanup será feito automaticamente pelos smart pointers
}

bool JITEngine::initialize() {
    // Criar LLJIT instance
    auto jit_or_error = llvm::orc::LLJITBuilder().create();
    if (!jit_or_error) {
        std::cerr << "Failed to create LLJIT: " 
                  << llvm::toString(jit_or_error.takeError()) << "\n";
        return false;
    }
    
    jit = std::move(*jit_or_error);
    return true;
}

bool JITEngine::add_module(std::unique_ptr<llvm::Module> module, ExecutionUnitId unit_id) {
    if (!jit) {
        std::cerr << "JIT engine not initialized\n";
        return false;
    }
    
    // Gerar função wrapper para execução automática ANTES de mover o módulo
    // O módulo precisa ser modificado antes de ser movido para o ThreadSafeModule
    std::string wrapper_name = generate_wrapper_function(*module, unit_id);
    unit_wrapper_functions[unit_id] = wrapper_name;
    
    // Criar ThreadSafeModule usando o contexto do módulo original
    auto tsm = create_thread_safe_module(std::move(module));
    
    // Adicionar ao JIT
    auto rt = jit->getMainJITDylib().createResourceTracker();
    auto err = jit->addIRModule(rt, std::move(tsm));
    
    if (err) {
        std::cerr << "Failed to add module to JIT: " 
                  << llvm::toString(std::move(err)) << "\n";
        return false;
    }
    
    // Registrar tracker
    unit_trackers[unit_id] = rt;
    
    return true;
}

void JITEngine::remove_module(ExecutionUnitId unit_id) {
    auto it = unit_trackers.find(unit_id);
    if (it != unit_trackers.end()) {
        // Remover módulo do JIT
        auto err = it->second->remove();
        if (err) {
            std::cerr << "Warning: Failed to remove module: " 
                      << llvm::toString(std::move(err)) << "\n";
        }
        unit_trackers.erase(it);
    }
    
    // Remover função wrapper
    unit_wrapper_functions.erase(unit_id);
}

void* JITEngine::execute_unit(ExecutionUnitId unit_id) {
    if (!jit) {
        return nullptr;
    }
    
    // Buscar função wrapper gerada para esta unidade
    auto wrapper_it = unit_wrapper_functions.find(unit_id);
    if (wrapper_it == unit_wrapper_functions.end()) {
        // Nenhuma função wrapper encontrada - unidade não foi compilada ou foi removida
        return nullptr;
    }
    
    const std::string& wrapper_name = wrapper_it->second;
    
    // Buscar endereço da função wrapper no JIT
    auto symbol_or_error = jit->lookup(wrapper_name);
    if (!symbol_or_error) {
        // Função não encontrada no JIT
        return nullptr;
    }
    
    // Executar função wrapper
    // A função wrapper retorna void* (ponteiro genérico)
    using WrapperFunc = void* (*)();
    // ExecutorAddr pode ser convertido para ponteiro usando toPtr<>
    WrapperFunc wrapper_func = symbol_or_error->toPtr<WrapperFunc>();
    
    try {
        void* result = wrapper_func();
        return result;
    } catch (...) {
        // Erro durante execução
        return nullptr;
    }
}

void* JITEngine::get_symbol_address(const std::string& symbol_name) {
    if (!jit) {
        return nullptr;
    }
    
    auto symbol_or_error = jit->lookup(symbol_name);
    if (!symbol_or_error) {
        return nullptr;
    }
    
    return (void*)symbol_or_error->toPtr<void*>();
}

llvm::orc::ThreadSafeModule JITEngine::create_thread_safe_module(std::unique_ptr<llvm::Module> module) {
    // Obter o contexto do módulo original
    llvm::LLVMContext& ctx = module->getContext();
    
    // Criar ThreadSafeContext usando o contexto do módulo
    // O módulo mantém uma referência ao seu contexto, então precisamos
    // criar um ThreadSafeContext que gerencia esse contexto de forma thread-safe
    auto tsc = std::make_shared<llvm::orc::ThreadSafeContext>(std::make_unique<llvm::LLVMContext>());
    
    // Criar ThreadSafeModule - o construtor aceita o módulo e o ThreadSafeContext
    // O módulo será movido para o ThreadSafeModule
    return llvm::orc::ThreadSafeModule(std::move(module), *tsc);
}

std::string JITEngine::generate_wrapper_function(llvm::Module& module, ExecutionUnitId unit_id) {
    // Gerar nome único para a função wrapper
    std::string wrapper_name = "nv_unit_wrapper_" + std::to_string(unit_id);
    
    // Obter contexto LLVM do módulo
    llvm::LLVMContext& ctx = module.getContext();
    
    // Criar tipo de retorno: void* (ponteiro genérico para resultado)
    llvm::PointerType* void_ptr_type = llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(ctx));
    llvm::FunctionType* wrapper_type = llvm::FunctionType::get(void_ptr_type, false);
    
    // Criar função wrapper
    llvm::Function* wrapper_func = llvm::Function::Create(
        wrapper_type,
        llvm::Function::ExternalLinkage,
        wrapper_name,
        module
    );
    
    // Criar bloco básico de entrada
    llvm::BasicBlock* entry_block = llvm::BasicBlock::Create(ctx, "entry", wrapper_func);
    llvm::IRBuilder<> builder(ctx);
    builder.SetInsertPoint(entry_block);
    
    // O código gerado pelo generate_ir coloca valores na eval_stack do contexto
    // Como não temos acesso direto à stack aqui, vamos criar uma função que:
    // 1. Chama a função de inicialização de globais (se existir)
    // 2. Executa o código principal (que já foi gerado no módulo)
    // 3. Retorna nullptr por enquanto (pode ser expandido para retornar último valor)
    
    // Chamar função de inicialização de globais se existir
    std::string init_func_name = "nv.global.init.65535";
    llvm::Function* init_func = module.getFunction(init_func_name);
    if (init_func) {
        builder.CreateCall(init_func);
    }
    
    // Por enquanto, retornar nullptr
    // No futuro, podemos:
    // - Acessar o último valor da eval_stack se disponível
    // - Retornar um ponteiro para o resultado
    // - Criar uma variável global para armazenar o resultado
    
    builder.CreateRet(llvm::ConstantPointerNull::get(void_ptr_type));
    
    return wrapper_name;
}

void JITEngine::clear() {
    // Remover todos os módulos do JIT
    std::vector<ExecutionUnitId> units_to_remove;
    for (const auto& [unit_id, tracker] : unit_trackers) {
        units_to_remove.push_back(unit_id);
    }
    
    for (ExecutionUnitId unit_id : units_to_remove) {
        remove_module(unit_id);
    }
    
    // Limpar rastreamentos
    unit_trackers.clear();
    unit_wrapper_functions.clear();
}

std::unordered_set<ExecutionUnitId> JITEngine::get_registered_units() const {
    std::unordered_set<ExecutionUnitId> units;
    for (const auto& [unit_id, tracker] : unit_trackers) {
        units.insert(unit_id);
    }
    return units;
}

} // namespace interactive
} // namespace nv
