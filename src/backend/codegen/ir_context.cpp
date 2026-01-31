#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/checker/unification.hpp"
#include <optional>

namespace nv {

llvm::Function* IRGenerationContext::ensure_runtime_func(const std::string& name,
                                                        llvm::ArrayRef<llvm::Type*> paramTypes,
                                                        llvm::Type* retTy) {
    auto* FT = llvm::FunctionType::get(retTy ? retTy : llvm::Type::getVoidTy(llvm_context), paramTypes, false);
    auto decl = module.getOrInsertFunction(name, FT);
    return llvm::cast<llvm::Function>(decl.getCallee());
}

std::shared_ptr<Type> IRGenerationContext::resolve_type(std::shared_ptr<Type> nv_type) {
    if (!nv_type) {
        return nullptr;
    }
    
    // Se for tipo polimórfico, instanciar com novas variáveis de tipo
    if (nv_type->kind == Kind::POLY_TYPE) {
        auto poly = std::static_pointer_cast<PolyType>(nv_type);
        // Obter contexto de unificação do checker se disponível
        if (type_checker_ptr) {
            auto* checker = static_cast<Checker*>(type_checker_ptr);
            int next_id = checker->unify_ctx.get_next_var_id();
            return poly->instantiate(next_id);
        }
        // Se não houver checker, usar ID inicial 0 (não ideal, mas funcional)
        static int fallback_id = 0;
        return poly->instantiate(fallback_id);
    }
    
    // Se for variável de tipo, resolver através do contexto de unificação
    if (nv_type->kind == Kind::TYPE_VAR) {
        auto tv = std::static_pointer_cast<TypeVar>(nv_type);
        // Resolver variável de tipo (path compression)
        auto resolved = tv->resolve();
        
        // Se ainda for variável de tipo não resolvida, tentar resolver através do checker
        if (resolved->kind == Kind::TYPE_VAR && type_checker_ptr) {
            auto* checker = static_cast<Checker*>(type_checker_ptr);
            resolved = checker->unify_ctx.resolve(resolved);
        }
        
        // Se ainda for variável de tipo não resolvida, usar tipo padrão (int)
        // Isso pode acontecer se a inferência não foi completa
        if (resolved->kind == Kind::TYPE_VAR) {
            // Em produção, isso deveria ser um erro, mas para compatibilidade
            // retornamos int como fallback
            return std::make_shared<Int>();
        }
        
        return resolved;
    }
    
    // Para tipos compostos, resolver recursivamente
    switch (nv_type->kind) {
        case Kind::ARRAY: {
            auto array_type = std::static_pointer_cast<Array>(nv_type);
            if (array_type && array_type->element_type) {
                auto resolved_elem = resolve_type(array_type->element_type);
                return std::make_shared<Array>(resolved_elem, array_type->size);
            }
            return nv_type;
        }
        case Kind::VECTOR: {
            // Vector não precisa resolução (não tem variáveis de tipo)
            return nv_type;
        }
        case Kind::TUPLE: {
            auto tuple_type = std::static_pointer_cast<Tuple>(nv_type);
            if (tuple_type) {
                std::vector<std::shared_ptr<Type>> resolved_elems;
                for (const auto& elem : tuple_type->element_type) {
                    resolved_elems.push_back(resolve_type(elem));
                }
                return std::make_shared<Tuple>(resolved_elems);
            }
            return nv_type;
        }
        case Kind::LABEL: {
            auto label_type = std::static_pointer_cast<Label>(nv_type);
            if (label_type) {
                std::vector<std::shared_ptr<Type>> resolved_params;
                for (const auto& param : label_type->paramstype) {
                    resolved_params.push_back(resolve_type(param));
                }
                auto resolved_ret = resolve_type(label_type->returntype);
                return std::make_shared<Label>(resolved_params, resolved_ret);
            }
            return nv_type;
        }
        default:
            // Tipos básicos não precisam resolução
            return nv_type;
    }
}

llvm::Type* IRGenerationContext::nv_type_to_llvm(std::shared_ptr<Type> nv_type) {
    if (!nv_type) {
        return llvm::Type::getVoidTy(llvm_context);
    }
    
    // Resolver tipo antes de converter (resolve variáveis de tipo e instancia polimórficos)
    nv_type = resolve_type(nv_type);
    
    if (!nv_type) {
        return llvm::Type::getVoidTy(llvm_context);
    }

    switch (nv_type->kind) {
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
            auto array_type = std::static_pointer_cast<Array>(nv_type);
            if (array_type && array_type->element_type) {
                auto elem_type = nv_type_to_llvm(array_type->element_type);
                // Array como estrutura ou ponteiro para o primeiro elemento
                // Por enquanto, retornamos ponteiro para o elemento
                return llvm::PointerType::get(elem_type, 0);
            }
            return llvm::Type::getInt8Ty(llvm_context); // fallback
        }
        
        case Kind::VECTOR: {
            // Vector usa Vector no runtime (tamanho variável, heterogêneo)
            // Retornar ponteiro genérico (será tratado como Value* no runtime)
            return ir_utils::get_value_ptr(*this);
        }
        
        case Kind::TUPLE: {
            auto tuple_type = std::static_pointer_cast<Tuple>(nv_type);
            if (tuple_type) {
                std::vector<llvm::Type*> elem_types;
                for (const auto& elem : tuple_type->element_type) {
                    elem_types.push_back(nv_type_to_llvm(elem));
                }
                return llvm::StructType::get(llvm_context, elem_types);
            }
            return llvm::Type::getVoidTy(llvm_context); // fallback
        }
        
        case Kind::LABEL: {
            auto label_type = std::static_pointer_cast<Label>(nv_type);
            if (label_type) {
                std::vector<llvm::Type*> param_types;
                for (const auto& param : label_type->paramstype) {
                    param_types.push_back(nv_type_to_llvm(param));
                }
                auto ret_type = nv_type_to_llvm(label_type->returntype);
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

void IRGenerationContext::emit_local_variable_dbg(
    llvm::AllocaInst* alloca,
    const std::string& name,
    const PositionData* pos
) {
    // Disabled for now: emitting dbg.declare for locals triggers a crash
    // in LLVM's DwarfDebug on this toolchain. We keep function-level
    // debug info (DISubprogram + locations) only.
    (void)alloca;
    (void)name;
    (void)pos;
}

llvm::AllocaInst* IRGenerationContext::create_alloca(llvm::Type* type, const std::string& name) {
    // Cria alocação no início da função atual
    if (!current_function) {
        // Se não há função atual, cria na posição atual
        return builder.CreateAlloca(type, nullptr, name.empty() ? "" : name);
    }
    
    auto* entry_block = &current_function->getEntryBlock();
    llvm::IRBuilder<llvm::NoFolder> tmp_builder(entry_block, entry_block->begin());
    
    return tmp_builder.CreateAlloca(type, nullptr, name.empty() ? "" : name);
}

llvm::AllocaInst* IRGenerationContext::create_and_register_variable(
    const std::string& name,
    llvm::Type* llvm_type,
    std::shared_ptr<Type> nv_type,
    bool is_constant
) {
    auto alloca = create_alloca(llvm_type, name);
    
    SymbolInfo info(alloca, llvm_type, nv_type, true, is_constant);
    symbol_table.define_symbol(name, info);
    
    emit_local_variable_dbg(alloca, name, nullptr);

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
    SymbolInfo new_info(value, value->getType(), info.nv_type, false, info.is_constant);
    return symbol_table.update_symbol(name, new_info);
}

std::optional<SymbolInfo> IRGenerationContext::get_symbol_info(const std::string& name) {
    return symbol_table.lookup_symbol(name);
}

}