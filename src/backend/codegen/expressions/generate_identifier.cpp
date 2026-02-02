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
        // Error: variable not declared - este erro já deve ter sido reportado pelo checker
        // Apenas retornar nullptr para evitar crash
        context.push_value(nullptr);
        return;
    }

    const nv::SymbolInfo& info = symbol_opt.value();

    // Se info.value for nullptr, significa que foi registrado por import mas ainda não foi criado
    // Neste caso, tentar buscar a variável global ou função diretamente no módulo
    // NOTA: Se foi importado com alias, precisamos buscar pelo nome original, não pelo alias
    // Mas não temos acesso ao nome original aqui, então tentamos ambos
    llvm::Value* actual_value = info.value;
    if (!actual_value) {
        auto& M = context.get_module();
        // Tentar buscar pelo símbolo atual (pode ser alias)
        auto* global_var = M.getGlobalVariable(symbol);
        auto* function = M.getFunction(symbol);
        
        // Se não encontrou, pode ser que seja um alias - neste caso, o módulo manager
        // deveria ter resolvido isso, mas vamos tentar buscar de outras formas
        // (por exemplo, procurando em todas as funções/variáveis globais)
        // Na prática, isso não deveria acontecer se o import foi feito corretamente
        
        if (global_var) {
            // Encontrou a variável global, atualizar a entrada na tabela de símbolos
            nv::SymbolInfo updated_info = info;
            updated_info.value = global_var;
            updated_info.llvm_type = nv::ir_utils::get_value_struct(context);
            updated_info.is_constant = false;
            context.get_symbol_table().define_symbol(symbol, updated_info);
            actual_value = global_var;
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
    
    // Se for uma alocação (variável local) ou global, precisamos carregar o valor
    if (info.is_allocated) {
        // Variável local (AllocaInst)
        auto* v = context.get_builder().CreateLoad(info.llvm_type, actual_value, symbol.c_str());
        context.push_value(v);
        return;
    } else if (llvm::isa<llvm::GlobalVariable>(actual_value)) {
        // Variável global: carregar o valor do GlobalVariable
        // O GlobalVariable deve estar inicializado corretamente via @llvm.global_ctors
        // com a tag de tipo correta
        auto& B = context.get_builder();
        auto* ValueTy = nv::ir_utils::get_value_struct(context);
        auto* ValuePtr = nv::ir_utils::get_value_ptr(context);
        
        // Carregar o valor
        auto* loaded_value = B.CreateLoad(info.llvm_type, actual_value, symbol.c_str());
        
        // Garantir que o tipo está correto usando ensure_value_type
        // Criar alloca temporário para passar para ensure_value_type
        auto* temp_alloca = B.CreateAlloca(ValueTy, nullptr, symbol + "_ensure");
        B.CreateStore(loaded_value, temp_alloca);
        
        // Chamar ensure_value_type para garantir que a tag está correta
        auto* ensure_func = context.ensure_runtime_func("ensure_value_type", {ValuePtr});
        B.CreateCall(ensure_func, {temp_alloca});
        
        // Carregar o valor garantido
        auto* ensured_value = B.CreateLoad(ValueTy, temp_alloca, symbol + "_ensured");
        context.push_value(ensured_value);
        return;
    }
    // Caso contrário, é uma constante ou valor direto (ex: parâmetro de função)
    context.push_value(actual_value);
}