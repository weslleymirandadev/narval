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

void IRGenerationContext::register_global_init(llvm::GlobalVariable* global, llvm::Value* init_value, const std::string& symbol_name) {
    pending_global_inits.push_back({global, init_value, symbol_name});
}

void IRGenerationContext::finalize_global_inits(int priority) {
    if (pending_global_inits.empty()) {
        return;
    }
    
    // Criar função de inicialização
    // IMPORTANTE: Usar nome único baseado na prioridade para evitar conflitos
    auto* VoidTy = llvm::Type::getVoidTy(llvm_context);
    auto* InitFuncTy = llvm::FunctionType::get(VoidTy, false);
    std::string func_name = "nv.global.init." + std::to_string(priority);
    auto* InitFunc = llvm::Function::Create(
        InitFuncTy,
        llvm::Function::InternalLinkage,
        func_name,
        module
    );
    
    // Criar basic block para a função
    auto* EntryBB = llvm::BasicBlock::Create(llvm_context, "entry", InitFunc);
    auto OldBuilder = builder.GetInsertPoint();
    auto* OldBB = builder.GetInsertBlock();
    builder.SetInsertPoint(EntryBB);
    
    auto* ValueTy = ir_utils::get_value_struct(*this);
    auto* ValuePtr = ir_utils::get_value_ptr(*this);
    auto* I32 = llvm::Type::getInt32Ty(llvm_context);
    auto* F64 = llvm::Type::getDoubleTy(llvm_context);
    
    // Executar todas as inicializações pendentes
    auto* I8Ptr = ir_utils::get_i8_ptr(*this);
    for (const auto& init : pending_global_inits) {
        // Criar ponteiro para o GlobalVariable (Value*)
        auto* global_ptr = builder.CreateBitCast(init.global, ValuePtr);
        
        // Embrulhar valor primitivo em Value struct diretamente no GlobalVariable
        // Isso garante que create_int/create_float/etc. escrevam diretamente no GlobalVariable
        if (init.init_value->getType() == ValueTy) {
            // Já é Value, apenas copiar diretamente
            builder.CreateStore(init.init_value, init.global);
        } else if (init.init_value->getType()->isIntegerTy(1)) {
            // bool
            auto* f = ensure_runtime_func("create_bool", {ValuePtr, I32});
            builder.CreateCall(f, {global_ptr, builder.CreateZExt(init.init_value, I32)});
        } else if (init.init_value->getType()->isIntegerTy()) {
            // int
            auto* f = ensure_runtime_func("create_int", {ValuePtr, I32});
            llvm::Value* iv = init.init_value->getType()->isIntegerTy(32) ? init.init_value : builder.CreateSExtOrTrunc(init.init_value, I32);
            builder.CreateCall(f, {global_ptr, iv});
        } else if (init.init_value->getType()->isFloatingPointTy()) {
            // float
            auto* f = ensure_runtime_func("create_float", {ValuePtr, F64});
            llvm::Value* fp = init.init_value->getType() == F64 ? init.init_value : builder.CreateFPExt(init.init_value, F64);
            builder.CreateCall(f, {global_ptr, fp});
        } else if (init.init_value->getType() == I8Ptr) {
            // string
            auto* f = ensure_runtime_func("create_str", {ValuePtr, I8Ptr});
            builder.CreateCall(f, {global_ptr, init.init_value});
        }
    }
    
    // Retornar void
    builder.CreateRetVoid();
    
    // Restaurar builder para o estado anterior
    if (OldBB && OldBuilder != OldBB->end()) {
        builder.SetInsertPoint(OldBuilder);
    } else if (current_function && !current_function->empty()) {
        // Se não conseguimos restaurar o ponto exato, restaurar para o último bloco da função atual
        builder.SetInsertPoint(&current_function->back());
    } else {
        // Se não há função atual, limpar o insertion point
        builder.ClearInsertionPoint();
    }
    
    // Registrar em @llvm.global_ctors usando AppendingLinkage
    // Isso permite que múltiplos módulos registrem inicializações
    // O linker vai combinar todas automaticamente
    auto* I32Ty = llvm::Type::getInt32Ty(llvm_context);
    auto* FuncPtrTy = llvm::PointerType::getUnqual(InitFuncTy);
    auto* I8PtrTy = llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(llvm_context));
    auto* CtorStructTy = llvm::StructType::get(llvm_context, {I32Ty, FuncPtrTy, I8PtrTy});
    auto* CtorArrayTy = llvm::ArrayType::get(CtorStructTy, 1);
    
    auto* PriorityConst = llvm::ConstantInt::get(I32Ty, priority);
    auto* FuncConst = llvm::ConstantExpr::getBitCast(InitFunc, FuncPtrTy);
    auto* NullConst = llvm::Constant::getNullValue(I8PtrTy);
    auto* CtorStruct = llvm::ConstantStruct::get(CtorStructTy, {PriorityConst, FuncConst, NullConst});
    auto* CtorArray = llvm::ConstantArray::get(CtorArrayTy, {CtorStruct});
    
    // Criar ou obter @llvm.global_ctors com AppendingLinkage
    // AppendingLinkage permite que múltiplos módulos adicionem entradas
    auto* GlobalCtors = module.getNamedGlobal("llvm.global_ctors");
    if (!GlobalCtors) {
        GlobalCtors = new llvm::GlobalVariable(
            module,
            CtorArrayTy,
            false,  // não constante
            llvm::GlobalValue::AppendingLinkage,
            CtorArray,
            "llvm.global_ctors"
        );
    } else {
        // Se já existe, precisamos adicionar nossa entrada ao array existente
        // Mas AppendingLinkage faz isso automaticamente no linker, então apenas criar uma nova entrada
        // Na prática, cada módulo cria seu próprio @llvm.global_ctors e o linker combina
        // Por enquanto, vamos criar uma nova entrada mesmo que já exista
        // O linker vai combinar todas as entradas automaticamente
    }
    
    // Limpar lista pendente
    pending_global_inits.clear();
}

}