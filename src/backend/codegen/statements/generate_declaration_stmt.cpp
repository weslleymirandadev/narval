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

    // Se não conseguiu obter tipo do checker, usar método tradicional
    if (!decl_ty) {
        decl_ty = nv::ir_utils::llvm_type_from_string(context, typ);
        if (!decl_ty) return;
    }

    // Detectar se estamos no nível superior (variável global)
    bool is_global = (context.get_current_function() == nullptr);
    
    llvm::Value* init_val = nullptr;
    if (value) {
        value->codegen(context);
        init_val = context.pop_value();
        if (!init_val) return;
    }

    auto* ValueTy = nv::ir_utils::get_value_struct(context);
    llvm::Type* stored_ty = decl_ty;
    llvm::Value* storage = nullptr;

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
            // Criar variável global com inicializador constante (se disponível)
            global = new llvm::GlobalVariable(
                M, ValueTy, false,  // não constante
                llvm::GlobalValue::InternalLinkage,  // InternalLinkage para variáveis definidas no mesmo módulo
                initializer,  // usar constante com tag ou zero
                symbol
            );
            // Se usamos uma constante (não zero), já está inicializado
            constant_initialized = (initializer != llvm::Constant::getNullValue(ValueTy));
        } else {
            // Se o GlobalVariable já existe (criado por import), precisamos atualizar o inicializador
            // setInitializer funciona mesmo se o GlobalVariable já foi referenciado, desde que não tenha sido modificado
            if (initializer != llvm::Constant::getNullValue(ValueTy)) {
                // Tentar atualizar o inicializador
                auto* current_init = global->getInitializer();
                // Verificar se o inicializador atual é zero/null usando comparação mais robusta
                bool is_null_init = false;
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
        
        // IMPORTANTE: Registrar para inicialização via @llvm.global_ctors apenas se não foi inicializado com constante
        // Se já foi inicializado com constante, não precisa de inicialização runtime
        // A função de inicialização será chamada explicitamente no início de main.start
        // Isso garante que a tag de tipo seja sempre definida corretamente
        if (init_val != nullptr && !constant_initialized) {
            context.register_global_init(global, init_val, symbol);
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
            constant
        );
        context.get_symbol_table().define_symbol(symbol, info);
    }
    // Para variáveis globais, o registro já foi feito acima quando verificamos se já existia
}
