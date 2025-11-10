#include "backend/codegen/ir_context.hpp"
#include <optional>

namespace rph {

llvm::Type* IRGenerationContext::rph_type_to_llvm(std::shared_ptr<Type> rph_type) {
    if (!rph_type) {
        return llvm::Type::getVoidTy(llvm_context);
    }

    switch (rph_type->kind) {
        case Kind::INT:
            return llvm::Type::getInt32Ty(llvm_context);
        
        case Kind::FLOAT:
            return llvm::Type::getDoubleTy(llvm_context);
        
        case Kind::BOOL:
            return llvm::Type::getInt1Ty(llvm_context);
        
        case Kind::VOID:
            return llvm::Type::getVoidTy(llvm_context);
        
        case Kind::STRING:
            return llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(llvm_context));
        
        case Kind::ARRAY: {
            auto array_type = std::static_pointer_cast<Array>(rph_type);
            if (array_type && array_type->element_type) {
                auto elem_type = rph_type_to_llvm(array_type->element_type);
                // Array como estrutura ou ponteiro para o primeiro elemento
                // Por enquanto, retornamos ponteiro para o elemento
                return llvm::PointerType::get(elem_type, 0);
            }
            return llvm::Type::getInt8Ty(llvm_context); // fallback
        }
        
        case Kind::TUPLE: {
            auto tuple_type = std::static_pointer_cast<Tuple>(rph_type);
            if (tuple_type) {
                std::vector<llvm::Type*> elem_types;
                for (const auto& elem : tuple_type->element_type) {
                    elem_types.push_back(rph_type_to_llvm(elem));
                }
                return llvm::StructType::get(llvm_context, elem_types);
            }
            return llvm::Type::getVoidTy(llvm_context); // fallback
        }
        
        case Kind::LABEL: {
            auto label_type = std::static_pointer_cast<Label>(rph_type);
            if (label_type) {
                std::vector<llvm::Type*> param_types;
                for (const auto& param : label_type->paramstype) {
                    param_types.push_back(rph_type_to_llvm(param));
                }
                auto ret_type = rph_type_to_llvm(label_type->returntype);
                return llvm::FunctionType::get(ret_type, param_types, false)->getPointerTo();
            }
            return llvm::Type::getVoidTy(llvm_context); // fallback
        }

        case Kind::ERROR:
        default:
            return llvm::Type::getVoidTy(llvm_context);
    }
}

llvm::Type* IRGenerationContext::get_llvm_type(Kind kind) {
    auto it = type_cache.find(kind);
    if (it != type_cache.end()) {
        return it->second;
    }
    return llvm::Type::getVoidTy(llvm_context);
}

llvm::AllocaInst* IRGenerationContext::create_alloca(llvm::Type* type, const std::string& name) {
    // Cria alocação no início da função atual
    if (!current_function) {
        // Se não há função atual, cria na posição atual
        return builder.CreateAlloca(type, nullptr, name.empty() ? nullptr : name.c_str());
    }
    
    auto* entry_block = &current_function->getEntryBlock();
    llvm::IRBuilder<llvm::NoFolder> tmp_builder(entry_block, entry_block->begin());
    
    return tmp_builder.CreateAlloca(type, nullptr, name.empty() ? nullptr : name.c_str());
}

llvm::AllocaInst* IRGenerationContext::create_and_register_variable(
    const std::string& name,
    llvm::Type* llvm_type,
    std::shared_ptr<Type> rph_type,
    bool is_constant
) {
    auto alloca = create_alloca(llvm_type, name);
    
    SymbolInfo info(alloca, llvm_type, rph_type, true, is_constant);
    symbol_table.define_symbol(name, info);
    
    return alloca;
}

llvm::Value* IRGenerationContext::load_symbol(const std::string& name) {
    auto info_opt = symbol_table.lookup_symbol(name);
    if (!info_opt.has_value()) {
        return nullptr;
    }
    
    auto info = info_opt.value();
    
    // Se for uma alocação, carrega o valor
    if (info.is_allocated && llvm::isa<llvm::AllocaInst>(info.value)) {
        return builder.CreateLoad(info.llvm_type, info.value, name + "_loaded");
    }
    
    // Caso contrário, retorna o valor diretamente
    return info.value;
}

bool IRGenerationContext::store_symbol(const std::string& name, llvm::Value* value) {
    auto info_opt = symbol_table.lookup_symbol(name);
    if (!info_opt.has_value()) {
        return false;
    }
    
    auto info = info_opt.value();
    
    // Se for uma alocação, armazena o valor
    if (info.is_allocated && llvm::isa<llvm::AllocaInst>(info.value)) {
        builder.CreateStore(value, info.value);
        return true;
    }
    
    // Tenta atualizar o símbolo (pode não ser uma alocação)
    SymbolInfo new_info(value, value->getType(), info.rph_type, false, info.is_constant);
    return symbol_table.update_symbol(name, new_info);
}

std::optional<SymbolInfo> IRGenerationContext::get_symbol_info(const std::string& name) {
    return symbol_table.lookup_symbol(name);
}

}