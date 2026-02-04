#include "frontend/ast/expressions/identifier_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <llvm/Support/raw_ostream.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

constexpr const char* ANSI_BOLD = "\x1b[1m";
constexpr const char* ANSI_RESET = "\x1b[0m";
constexpr const char* ANSI_RED = "\x1b[31m";

namespace {
    // Converte um caminho relativo em absoluto (mesma lógica do checker)
    std::string to_absolute_path(const std::string& path) {
        if (path.empty()) return path;
        try {
            std::filesystem::path file_path(path);
            if (file_path.is_absolute()) {
                try { return std::filesystem::canonical(file_path).string(); }
                catch (const std::filesystem::filesystem_error&) { return std::filesystem::absolute(file_path).string(); }
            }
            try { return std::filesystem::canonical(std::filesystem::absolute(file_path)).string(); }
            catch (const std::filesystem::filesystem_error&) { return std::filesystem::absolute(file_path).string(); }
        } catch (const std::exception&) {
            return path;
        }
    }
    
    // Reporta erro no mesmo formato do checker/parser
    void report_error(const std::string& filename, const PositionData* pos, const std::string& message) {
        std::string abs_filename = to_absolute_path(filename);
        
        if (!pos || pos->line == 0) {
            std::cerr << ANSI_BOLD << abs_filename << ": "
                      << ANSI_RED << "ERROR" << ANSI_RESET << ANSI_BOLD << ": "
                      << message << ANSI_RESET << "\n\n";
            return;
        }
        
        // Ler linhas do arquivo para contexto
        std::vector<std::string> lines;
        std::ifstream file(abs_filename);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                lines.push_back(line);
            }
            file.close();
        }
        
        std::cerr << ANSI_BOLD << abs_filename << ":" << pos->line << ":" << pos->col[0] << ": "
                  << ANSI_RED << "ERROR" << ANSI_RESET << ANSI_BOLD << ": "
                  << message << ANSI_RESET << "\n";
        
        // Mostrar contexto (mesmo formato do checker)
        if (pos->line > 0 && pos->line - 1 < lines.size()) {
            std::string line_content = lines[pos->line - 1];
            std::replace(line_content.begin(), line_content.end(), '\n', ' ');
            std::cerr << " " << pos->line << " |   " << line_content << "\n";
            
            int line_width = pos->line > 0 ? static_cast<int>(std::log10(pos->line) + 1) : 1;
            std::cerr << std::string(line_width, ' ') << "  |";
            std::cerr << std::string(pos->col[0] - 1 + 3, ' ');
            
            std::cerr << ANSI_RED;
            for (size_t i = pos->col[0]; i < pos->col[1]; ++i) {
                std::cerr << "^";
            }
            std::cerr << ANSI_RESET << "\n\n";
        } else {
            std::cerr << "\n";
        }
        return;
    }
}

void IdentifierNode::codegen(nv::IRGenerationContext& context) {
    context.set_debug_location(position.get());
    
    auto symbol_opt = context.get_symbol_info(symbol);
    if (!symbol_opt) {
        // Intrínseco: 'json' é um objeto especial da linguagem
        if (symbol == "json") {
            auto* I8P = nv::ir_utils::get_i8_ptr(context);
            auto* nullJson = llvm::Constant::getNullValue(I8P);
            context.push_value(nullJson);
            return;
        }

        // No REPL, se não encontrar na tabela de símbolos, tentar buscar diretamente no módulo
        // Isso permite que variáveis declaradas em fragmentos anteriores sejam encontradas
        auto& M = context.get_module();
        auto* global_var = M.getGlobalVariable(symbol);
        
        if (global_var) {
            // Encontrou a variável global diretamente no módulo
            // Criar SymbolInfo e registrar na tabela
            auto* ValueTy = nv::ir_utils::get_value_struct(context);
            nv::SymbolInfo info(
                global_var,
                ValueTy,
                nullptr,
                false,  // não é alocação local
                false   // não é constante
            );
            context.get_symbol_table().define_symbol(symbol, info);
            symbol_opt = info;
        } else {
            // Modo incremental: o símbolo pode ter sido definido em um fragmento anterior.
            // Por enquanto, vamos apenas retornar nullptr para variáveis externas para evitar segfaults
            // TODO: Implementar resolução adequada de variáveis externas entre fragmentos
            context.push_value(nullptr);
            return;
        }
    }

    const nv::SymbolInfo& info = symbol_opt.value();

    // Se info.value for nullptr, significa que foi registrado por import mas ainda não foi criado
    // Neste caso, tentar buscar a variável global ou função diretamente no módulo
    // NOTA: Se foi importado com alias, precisamos buscar pelo nome original, não pelo alias
    // Mas não temos acesso ao nome original aqui, então tentamos ambos
    llvm::Value* actual_value = info.value;
    if (!actual_value) {
        // Para símbolos de fragmentos anteriores, tentar resolver via símbolo do JIT
        auto& M = context.get_module();
        auto* global_var = M.getGlobalVariable(symbol);
        auto* function = M.getFunction(symbol);
        
        if (global_var) {
            // Encontrou a variável global, atualizar a entrada na tabela de símbolos
            nv::SymbolInfo updated_info = info;
            updated_info.value = global_var;
            updated_info.llvm_type = nv::ir_utils::get_value_struct(context);
            updated_info.is_constant = false;
            context.get_symbol_table().define_symbol(symbol, updated_info);
            actual_value = global_var;
            
            // No REPL, variáveis globais podem não ter initializer inicialmente
            // Não verificar isso aqui - deixar o código tentar carregar
        } else if (function) {
            // Encontrou a função, atualizar a entrada na tabela de símbolos
            nv::SymbolInfo updated_info = info;
            updated_info.value = function;
            updated_info.llvm_type = function->getType();
            updated_info.is_constant = true;  // funções são constantes
            context.get_symbol_table().define_symbol(symbol, updated_info);
            actual_value = function;
        } else {
            // Variable/function not yet created - este erro já deve ter sido reportado pelo checker
            // Apenas retornar nullptr para evitar crash e duplicação de erros
            context.push_value(nullptr);
            return;
        }
    }

    // Se for uma função, retornar diretamente (não precisa carregar)
    if (llvm::isa<llvm::Function>(actual_value)) {
        context.push_value(actual_value);
        return;
    }
    
    llvm::Value* loaded_value = nullptr;
    
    // Se for uma alocação (variável local) ou global, precisamos carregar o valor
    if (info.is_allocated) {
        // Variável local (AllocaInst)
        loaded_value = context.get_builder().CreateLoad(info.llvm_type, actual_value, symbol.c_str());
    } else if (llvm::isa<llvm::GlobalVariable>(actual_value)) {
        // Variável global: carregar o valor do GlobalVariable
        // O GlobalVariable deve estar inicializado corretamente via @llvm.global_ctors
        // com a tag de tipo correta
        auto& B = context.get_builder();
        auto* ValueTy = nv::ir_utils::get_value_struct(context);
        auto* ValuePtr = nv::ir_utils::get_value_ptr(context);
        
        // Carregar o valor
        loaded_value = B.CreateLoad(info.llvm_type, actual_value, symbol.c_str());
        
        // Para variáveis globais do tipo Value, precisamos garantir que o tipo está correto
        // O ensure_value_type é necessário para garantir que a tag esteja correta
        if (info.llvm_type == ValueTy) {
            // É Value struct - garantir que tipo está correto usando ensure_value_type
            auto* temp_alloca = context.create_alloca(ValueTy, symbol + "_ensure");
            B.CreateStore(loaded_value, temp_alloca);
            
            // Chamar ensure_value_type para garantir que a tag está correta
            auto* ensure_func = context.ensure_runtime_func("ensure_value_type", {ValuePtr});
            B.CreateCall(ensure_func, {temp_alloca});
            
            // Carregar o valor garantido
            loaded_value = B.CreateLoad(ValueTy, temp_alloca, symbol + "_ensured");
        } else {
            // Para tipos não-Value, manter comportamento original
            // Criar alloca temporário para passar para ensure_value_type
            auto* temp_alloca = context.create_alloca(ValueTy, symbol + "_ensure");
            B.CreateStore(loaded_value, temp_alloca);
            
            // Chamar ensure_value_type para garantir que a tag está correta
            auto* ensure_func = context.ensure_runtime_func("ensure_value_type", {ValuePtr});
            B.CreateCall(ensure_func, {temp_alloca});
            
            // Carregar o valor garantido
            loaded_value = B.CreateLoad(ValueTy, temp_alloca, symbol + "_ensured");
        }
    } else {
        // Caso contrário, é uma constante ou valor direto (ex: parâmetro de função)
        loaded_value = actual_value;
    }
    
    // Register the variable value for REPL output (if in interactive mode)
    // DISABLED: nv_register_value function doesn't exist in runtime
    /*
    if (loaded_value) {
        auto& B = context.get_builder();
        auto* ValueTy = nv::ir_utils::get_value_struct(context);
        
        if (loaded_value->getType() == ValueTy) {
            // Value is already a Value struct, register it directly
            auto* register_fn = context.ensure_runtime_func("nv_register_value", {nv::ir_utils::get_value_ptr(context), nv::ir_utils::get_i8_ptr(context), nv::ir_utils::get_i8_ptr(context)});
            auto* alloca = context.create_alloca(ValueTy, symbol + "_register");
            B.CreateStore(loaded_value, alloca);
            
            // Create string literals for the variable name and source
            auto* symbol_str = B.CreateGlobalStringPtr(symbol);
            auto* source_str = B.CreateGlobalStringPtr("repl");
            
            B.CreateCall(register_fn, {alloca, symbol_str, source_str});
        }
    }
    */
    
    context.push_value(loaded_value);
}

// Ensure a single translation unit emits the vtable/typeinfo for IdentifierNode
IdentifierNode::~IdentifierNode() {}