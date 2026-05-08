#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include <llvm/IR/Constants.h>

void DeclarationStmtNode::codegen(nv::IRGenerationContext& context) {
    context.set_debug_location(position.get());
    auto* id_node = static_cast<IdentifierNode*>(target.get());
    const std::string& symbol = id_node->symbol;

    std::shared_ptr<nv::Type> nv_type = nullptr;
    llvm::Type* decl_ty = nullptr;

    // Tentar obter tipo inferido do checker se disponível
    if (context.get_type_checker()) {
        auto* checker = static_cast<nv::Checker*>(context.get_type_checker());
        try {
            // Obter tipo inferido/resolvido do checker
            if (typ == "automatic") {
                // Para tipo automático, usar inferência
                nv_type = checker->infer_expr(value.get());
            } else {
                // Para tipo explícito, verificar com o checker
                auto& checked_type = checker->check_node(this);
                nv_type = checked_type;
            }
            
            // Resolver tipo (resolve variáveis de tipo e instancia polimórficos)
            if (nv_type) {
                nv_type = context.resolve_type(nv_type);
                decl_ty = context.nv_type_to_llvm(nv_type);
            }
        } catch (std::exception& e) {
            // Se houver erro no checker, continuar com método tradicional
            // (pode ser que o checker não tenha sido executado ainda)
        }
    }

    llvm::Value* init_val = nullptr;
    if (value) {
        value->codegen(context);
        init_val = context.pop_value();
        if (!init_val) return;
    }

    // Se não conseguiu obter tipo do checker, usar método tradicional
    if (!decl_ty) {
        // Para tipo "automatic", tentar inferir a partir do valor inicial
        if (typ == "automatic" && init_val) {
            if (init_val->getType()->isFloatingPointTy()) {
                decl_ty = nv::ir_utils::get_f64(context);
            } else if (init_val->getType()->isIntegerTy()) {
                decl_ty = nv::ir_utils::get_i32(context);
            } else if (init_val->getType()->isIntegerTy(1)) {
                decl_ty = nv::ir_utils::get_i1(context);
            } else {
                // Default para i32 se não conseguir inferir
                decl_ty = nv::ir_utils::get_i32(context);
            }
        } else {
            decl_ty = nv::ir_utils::llvm_type_from_string(context, typ);
            if (!decl_ty) return;
        }
    }

    // Detectar se estamos no nível superior (variável global): sem função atual ou na função do programa (main.start)
    bool is_global = (context.get_current_function() == nullptr)
        || (context.get_program_function() != nullptr && context.get_current_function() == context.get_program_function());

    auto* ValueTy = nv::ir_utils::get_value_struct(context);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(context);
    llvm::Type* stored_ty = decl_ty;
    llvm::Value* storage = nullptr;

    // REPL: se o símbolo já existe como slot (Value*), gravar no slot (independente de is_global; no REPL estamos dentro do wrapper)
    auto existing_slot = context.get_symbol_table().lookup_symbol(symbol);
    if (existing_slot.has_value() && init_val) {
        const auto& info = existing_slot.value();
        if (info.value && info.value->getType() == ValuePtr && info.llvm_type == ValueTy) {
            auto& B = context.get_builder();
            auto& C = context.get_context();
            auto& M = context.get_module();
            auto* I32 = llvm::Type::getInt32Ty(C);
            auto* F64 = llvm::Type::getDoubleTy(C);
            
            // CORREÇÃO: Em vez de criar um tmp_alloca e depois carregar, passar diretamente o ponteiro global
            // Isso evita problemas de dominação pois não dependemos de valores temporários
            auto* global_ptr = B.CreateBitCast(info.value, ValuePtr);
            
            if (init_val->getType() == ValueTy) {
                // Já é Value, apenas copiar diretamente
                B.CreateStore(init_val, info.value);
                return;
            } else if (init_val->getType()->isIntegerTy(1)) {
                auto decl = M.getOrInsertFunction("create_bool", llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, I32}, false));
                B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {global_ptr, B.CreateZExt(init_val, I32)});
                return;
            } else if (init_val->getType()->isIntegerTy()) {
                auto decl = M.getOrInsertFunction("create_int", llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, I32}, false));
                llvm::Value* iv = init_val->getType()->isIntegerTy(32) ? init_val : B.CreateSExtOrTrunc(init_val, I32);
                B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {global_ptr, iv});
                return;
            } else if (init_val->getType()->isFloatingPointTy()) {
                auto decl = M.getOrInsertFunction("create_float", llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, F64}, false));
                llvm::Value* fp = init_val->getType() == F64 ? init_val : B.CreateFPExt(init_val, F64);
                B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {global_ptr, fp});
                return;
            } else if (init_val->getType() == nv::ir_utils::get_i8_ptr(context)) {
                auto decl = M.getOrInsertFunction("create_str", llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, nv::ir_utils::get_i8_ptr(context)}, false));
                B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {global_ptr, init_val});
                return;
            }
        }
    }

    // Se for variável global, criar como GlobalVariable e embrulhar valores primitivos em Value
    if (is_global) {
        // Para variáveis globais, sempre armazenar como Value para preservar tipo no runtime
        stored_ty = ValueTy;
        
        // Verificar se já existe um GlobalVariable com esse nome (pode ter sido criado por import)
        auto& M = context.get_module();
        llvm::GlobalVariable* global = M.getGlobalVariable(symbol);
        
        // Tentar criar uma constante Value com a tag correta se temos um valor constante
        // Usar função genérica que suporta qualquer tipo criado em tempo de compilação
        llvm::Constant* initializer = nullptr;
        if (init_val) {
            initializer = nv::ir_utils::create_value_constant_from_llvm_value(context, init_val, nv_type);
        }
        
        // Se não conseguimos criar uma constante, usar zero (será inicializado depois)
        if (!initializer) {
            initializer = llvm::Constant::getNullValue(ValueTy);
        }
        
        bool constant_initialized = false;
        if (!global) {
            // Criar variável global inicializada como zero
            global = new llvm::GlobalVariable(
                M, ValueTy, false,
                llvm::GlobalValue::ExternalLinkage,  // Usar ExternalLinkage para permitir acesso entre fragmentos
                llvm::Constant::getNullValue(ValueTy),  // inicializar como zero
                symbol
            );
        } else {
            // Se o GlobalVariable já existe (criado por import), precisamos atualizar o inicializador
            // setInitializer funciona mesmo se o GlobalVariable já foi referenciado, desde que não tenha sido modificado
            if (initializer != llvm::Constant::getNullValue(ValueTy)) {
                // Tentar atualizar o inicializador
                // Verificar se o inicializador atual é zero/null usando comparação mais robusta
                bool is_null_init = false;
                llvm::Constant* current_init = global->hasInitializer() ? global->getInitializer() : nullptr;

                if (!current_init) {
                    is_null_init = true;
                } else if (llvm::isa<llvm::ConstantAggregateZero>(current_init)) {
                    // É um agregado zero (struct zero)
                    is_null_init = true;
                } else if (current_init == llvm::Constant::getNullValue(ValueTy)) {
                    // Comparação direta de ponteiro
                    is_null_init = true;
                } else {
                    // Verificar se todos os campos são zero
                    if (auto* struct_const = llvm::dyn_cast<llvm::ConstantStruct>(current_init)) {
                        bool all_zero = true;
                        for (unsigned i = 0; i < struct_const->getNumOperands(); ++i) {
                            auto* field = struct_const->getOperand(i);
                            if (!llvm::isa<llvm::ConstantAggregateZero>(field) && 
                                !llvm::isa<llvm::ConstantPointerNull>(field) &&
                                field != llvm::ConstantInt::getNullValue(field->getType())) {
                                all_zero = false;
                                break;
                            }
                        }
                        is_null_init = all_zero;
                    }
                }
                
                if (is_null_init) {
                    // Ainda não foi inicializado (ou está zero), podemos atualizar
                    global->setInitializer(initializer);
                    constant_initialized = true;
                } else if (current_init == initializer) {
                    // Já está inicializado com a mesma constante
                    constant_initialized = true;
                }
            }
        }
        
        // IMPORTANTE: Se há um valor inicial não constante, inicializar a variável global diretamente
        // Isso evita problemas de dominação pois não usa @llvm.global_ctors
        if (init_val != nullptr && !constant_initialized) {
            auto& B = context.get_builder();
            auto& C = context.get_context();
            auto* I32 = llvm::Type::getInt32Ty(C);
            auto* F64 = llvm::Type::getDoubleTy(C);
            auto* ValuePtr = nv::ir_utils::get_value_ptr(context);
            auto* global_ptr = B.CreateBitCast(global, ValuePtr);

            if (init_val->getType() == ValueTy) {
                B.CreateStore(init_val, global);
            } else if (init_val->getType()->isIntegerTy(1)) {
                auto* f = context.ensure_runtime_func("create_bool", {ValuePtr, I32});
                B.CreateCall(f, {global_ptr, B.CreateZExt(init_val, I32)});
            } else if (init_val->getType()->isIntegerTy()) {
                auto* f = context.ensure_runtime_func("create_int", {ValuePtr, I32});
                llvm::Value* iv = init_val->getType()->isIntegerTy(32) ? init_val : B.CreateSExtOrTrunc(init_val, I32);
                B.CreateCall(f, {global_ptr, iv});
            } else if (init_val->getType()->isFloatingPointTy()) {
                auto* f = context.ensure_runtime_func("create_float", {ValuePtr, F64});
                llvm::Value* fp = init_val->getType() == F64 ? init_val : B.CreateFPExt(init_val, F64);
                B.CreateCall(f, {global_ptr, fp});
            } else if (init_val->getType()->isPointerTy()) {
                if (init_val->getType() == nv::ir_utils::get_i8_ptr(context)) {
                    auto* f = context.ensure_runtime_func("create_str", {ValuePtr, nv::ir_utils::get_i8_ptr(context)});
                    B.CreateCall(f, {global_ptr, init_val});
                }
            }
        }
        
        // Garantir que o GlobalVariable está registrado na tabela de símbolos
        // (pode ter sido criado por import mas não registrado ainda, ou vice-versa)
        auto existing_info = context.get_symbol_table().lookup_symbol(symbol);
        if (!existing_info.has_value() || existing_info.value().value != global) {
            // Registrar na tabela de símbolos se não estiver ou se for diferente
            nv::SymbolInfo info(
                global,
                decl_ty,
                nullptr,
                false,  // não é alocação local
                false   // não é constante
            );
            context.get_symbol_table().define_symbol(symbol, info);
        }
        
        storage = global;
    } else {
        // Variável local: comportamento original
        if (init_val && init_val->getType() == ValueTy) {
            stored_ty = ValueTy;
        }
        
        storage = context.create_alloca(stored_ty, symbol);
        
        if (init_val) {
            init_val = nv::ir_utils::promote_type(context, init_val, stored_ty);
            if (!init_val) return;
            context.get_builder().CreateStore(init_val, storage);
        }
        
        // Registrar variável local na tabela de símbolos
        nv::SymbolInfo info(
            storage,
            stored_ty,
            nv_type,
            true,  // is_allocated: true para locais
            mutable_
        );
        context.get_symbol_table().define_symbol(symbol, info);
    }
    // Para variáveis globais, o registro já foi feito acima quando verificamos se já existia
}
