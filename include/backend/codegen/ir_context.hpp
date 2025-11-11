#pragma once

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/NoFolder.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <string>
#include <stack>
#include <optional>
#include "frontend/checker/type.hpp"
#include "frontend/ast/ast.hpp"

// Forward declarations
namespace rph {
    class Checker;
}

namespace rph {

/**
 * Estrutura para armazenar informações de um símbolo LLVM
 */
struct SymbolInfo {
    llvm::Value* value;              // O valor LLVM (AllocaInst para variáveis, etc)
    llvm::Type* llvm_type;           // Tipo LLVM correspondente
    std::shared_ptr<Type> rph_type;   // Tipo Ruphi (do checker)
    bool is_allocated;                // Se é uma alocação (AllocaInst) ou não
    bool is_constant;                 // Se é constante

    // Necessário para uso com unordered_map::operator[]
    SymbolInfo()
        : value(nullptr), llvm_type(nullptr), rph_type(nullptr), is_allocated(false), is_constant(false) {}

    SymbolInfo(llvm::Value* val, llvm::Type* llvm_ty, std::shared_ptr<Type> rph_ty, bool allocated = false, bool constant = false)
        : value(val), llvm_type(llvm_ty), rph_type(rph_ty), is_allocated(allocated), is_constant(constant) {}
};

/**
 * Tabela de símbolos com escopos aninhados
 */
class SymbolTable {
private:
    struct Scope {
        std::unordered_map<std::string, SymbolInfo> symbols;
        std::shared_ptr<Scope> parent;
        
        Scope(std::shared_ptr<Scope> p = nullptr) : parent(p) {}
    };

    std::shared_ptr<Scope> current_scope;

public:
    SymbolTable() : current_scope(std::make_shared<Scope>()) {}

    /**
     * Entra em um novo escopo
     */
    void push_scope() {
        current_scope = std::make_shared<Scope>(current_scope);
    }

    /**
     * Sai do escopo atual
     */
    void pop_scope() {
        if (current_scope && current_scope->parent) {
            current_scope = current_scope->parent;
        }
    }

    /**
     * Define um símbolo no escopo atual
     */
    void define_symbol(const std::string& name, const SymbolInfo& info) {
        current_scope->symbols[name] = info;
    }

    /**
     * Atualiza um símbolo existente (procura em todos os escopos)
     */
    bool update_symbol(const std::string& name, const SymbolInfo& info) {
        auto scope = current_scope;
        while (scope) {
            if (scope->symbols.find(name) != scope->symbols.end()) {
                scope->symbols[name] = info;
                return true;
            }
            scope = scope->parent;
        }
        return false;
    }

    /**
     * Busca um símbolo (procura em todos os escopos)
     */
    std::optional<SymbolInfo> lookup_symbol(const std::string& name) const {
        auto scope = current_scope;
        while (scope) {
            auto it = scope->symbols.find(name);
            if (it != scope->symbols.end()) {
                return it->second;
            }
            scope = scope->parent;
        }
        return std::nullopt;
    }

    /**
     * Verifica se um símbolo existe no escopo atual (sem procurar nos pais)
     */
    bool exists_in_current_scope(const std::string& name) const {
        return current_scope->symbols.find(name) != current_scope->symbols.end();
    }

    /**
     * Verifica se um símbolo existe em qualquer escopo
     */
    bool exists(const std::string& name) const {
        return lookup_symbol(name).has_value();
    }
};

/**
 * Contexto de controle de fluxo para gerenciar loops, breaks e continues
 */
class ControlFlowContext {
private:
    struct LoopContext {
        llvm::BasicBlock* loop_header;     // Bloco de cabeçalho do loop
        llvm::BasicBlock* loop_body;       // Bloco do corpo do loop
        llvm::BasicBlock* loop_continue;  // Bloco de continue (incremento)
        llvm::BasicBlock* loop_exit;       // Bloco de saída (após o loop)
        std::string loop_name;             // Nome identificador do loop
    };

    std::stack<LoopContext> loop_stack;

public:
    /**
     * Entra em um novo loop
     */
    void enter_loop(const std::string& name, 
                    llvm::BasicBlock* header,
                    llvm::BasicBlock* body,
                    llvm::BasicBlock* continue_bb = nullptr,
                    llvm::BasicBlock* exit = nullptr) {
        LoopContext ctx;
        ctx.loop_name = name;
        ctx.loop_header = header;
        ctx.loop_body = body;
        ctx.loop_continue = continue_bb;
        ctx.loop_exit = exit;
        loop_stack.push(ctx);
    }

    /**
     * Sai do loop atual
     */
    void exit_loop() {
        if (!loop_stack.empty()) {
            loop_stack.pop();
        }
    }

    /**
     * Obtém o contexto do loop atual
     */
    std::optional<LoopContext> get_current_loop() const {
        if (loop_stack.empty()) {
            return std::nullopt;
        }
        return loop_stack.top();
    }

    /**
     * Verifica se estamos dentro de um loop
     */
    bool in_loop() const {
        return !loop_stack.empty();
    }

    /**
     * Obtém o bloco de saída do loop atual
     */
    llvm::BasicBlock* get_current_exit() const {
        if (auto loop = get_current_loop()) {
            return loop->loop_exit;
        }
        return nullptr;
    }

    /**
     * Obtém o bloco de continue do loop atual
     */
    llvm::BasicBlock* get_current_continue() const {
        if (auto loop = get_current_loop()) {
            return loop->loop_continue;
        }
        return nullptr;
    }
};

/**
 * Contexto principal de geração de IR
 * Gerencia todos os aspectos da geração de código LLVM
 */
class IRGenerationContext {
private:
    llvm::LLVMContext& llvm_context;
    llvm::Module& module;
    llvm::IRBuilder<llvm::NoFolder>& builder;

    SymbolTable symbol_table;
    ControlFlowContext control_flow;

    // Informações de tipo do checker (opcional, pode ser passado durante geração)
    // Usa ponteiro raw para evitar dependência circular
    void* type_checker_ptr;

    // Função atual sendo gerada
    llvm::Function* current_function;

    // Mapeamento de tipos Ruphi para tipos LLVM
    std::unordered_map<Kind, llvm::Type*> type_cache;

    // Pilha de avaliação para resultados de expressões
    std::vector<llvm::Value*> eval_stack;

public:
    IRGenerationContext(
        llvm::LLVMContext& ctx,
        llvm::Module& mod,
        llvm::IRBuilder<llvm::NoFolder>& bld,
        void* checker_ptr = nullptr
    ) : llvm_context(ctx), module(mod), builder(bld), type_checker_ptr(checker_ptr), current_function(nullptr) {
        initialize_type_cache();
    }

    // Acesso aos componentes LLVM
    llvm::LLVMContext& get_context() { return llvm_context; }
    llvm::Module& get_module() { return module; }
    llvm::IRBuilder<llvm::NoFolder>& get_builder() { return builder; }

    // Gerenciamento de símbolos
    SymbolTable& get_symbol_table() { return symbol_table; }

    // Gerenciamento de controle de fluxo
    ControlFlowContext& get_control_flow() { return control_flow; }

    // Gerenciamento de funções
    void set_current_function(llvm::Function* fn) { current_function = fn; }
    llvm::Function* get_current_function() { return current_function; }
    bool has_current_function() const { return current_function != nullptr; }

    // Pilha de avaliação
    void push_value(llvm::Value* v) { eval_stack.push_back(v); }
    llvm::Value* pop_value() {
        if (eval_stack.empty()) return nullptr;
        llvm::Value* v = eval_stack.back();
        eval_stack.pop_back();
        return v;
    }
    llvm::Value* peek_value() { return eval_stack.empty() ? nullptr : eval_stack.back(); }
    bool has_value() const { return !eval_stack.empty(); }

    // Checker de tipos
    void set_type_checker(void* checker) { type_checker_ptr = checker; }
    void* get_type_checker() { return type_checker_ptr; }

    /**
     * Converte um tipo Ruphi para um tipo LLVM
     */
    llvm::Type* rph_type_to_llvm(std::shared_ptr<Type> rph_type);

    /**
     * Obtém o tipo LLVM padrão para um tipo básico
     */
    llvm::Type* get_llvm_type(Kind kind);

    /**
     * Cria uma alocação de variável local
     */
    llvm::AllocaInst* create_alloca(llvm::Type* type, const std::string& name = "");

    /**
     * Cria uma alocação e registra como símbolo
     */
    llvm::AllocaInst* create_and_register_variable(
        const std::string& name,
        llvm::Type* llvm_type,
        std::shared_ptr<Type> rph_type,
        bool is_constant = false
    );

    /**
     * Obtém o valor de um símbolo (carrega se for AllocaInst)
     */
    llvm::Value* load_symbol(const std::string& name);

    /**
     * Armazena um valor em um símbolo
     */
    bool store_symbol(const std::string& name, llvm::Value* value);

    /**
     * Obtém informações de um símbolo
     */
    std::optional<SymbolInfo> get_symbol_info(const std::string& name);

    /**
     * Gerenciamento de escopos
     */
    void enter_scope() { symbol_table.push_scope(); }
    void exit_scope() { symbol_table.pop_scope(); }

    /**
     * Cria um novo bloco básico com nome
     */
    llvm::BasicBlock* create_block(const std::string& name) {
        return llvm::BasicBlock::Create(llvm_context, name, current_function);
    }

    llvm::Function* ensure_runtime_func(const std::string& name,
                                   llvm::ArrayRef<llvm::Type*> paramTypes,
                                   llvm::Type* retTy = nullptr);

    /**
     * Inicializa o cache de tipos LLVM
     */
private:
    void initialize_type_cache() {
        type_cache[Kind::INT] = llvm::Type::getInt32Ty(llvm_context);
        type_cache[Kind::FLOAT] = llvm::Type::getDoubleTy(llvm_context);
        type_cache[Kind::BOOL] = llvm::Type::getInt1Ty(llvm_context);
        type_cache[Kind::VOID] = llvm::Type::getVoidTy(llvm_context);
        
        // String é tratado como ponteiro para i8
        type_cache[Kind::STRING] = llvm::Type::getInt8Ty(llvm_context);
    }
};

} // namespace rph

