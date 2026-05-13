#include "frontend/ast/expressions/closure_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "backend/codegen/generate_ir.hpp"
#include "frontend/checker/checker.hpp"
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/DerivedTypes.h>
#include <sstream>

void ClosureExprNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    
    // Registrar features usadas
    nv::register_feature("closures");
    
    auto& builder = ctx.get_builder();
    auto& module = ctx.get_module();
    auto& llvm_context = ctx.get_context();
    
    // === Tipos LLVM ===
    auto* ValueTy = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
    auto* I32 = llvm::Type::getInt32Ty(llvm_context);
    auto* I8P = llvm::PointerType::getUnqual(llvm_context);
    auto* VoidTy = llvm::Type::getVoidTy(llvm_context);
    
    // === Gerar nome único para a função da closure ===
    static int closure_counter = 0;
    std::stringstream func_name;
    func_name << "__closure_fn_" << closure_counter++;
    std::string closure_func_name = func_name.str();
    
    // === Criar tipo da função ===
    // Assinatura: Value fn(Value* args, int argc, Value* captures, int capture_count)
    std::vector<llvm::Type*> param_types = {ValuePtr, I32, ValuePtr, I32};
    auto* closure_func_type = llvm::FunctionType::get(ValueTy, param_types, false);
    
    // === Criar função LLVM ===
    auto* closure_func = llvm::Function::Create(
        closure_func_type,
        llvm::Function::InternalLinkage,
        closure_func_name,
        module
    );
    
    // === SOLUÇÃO DEFINITIVA: Captura sem referências cruzadas ===
    std::vector<llvm::Value*> captured_values;
    for (const auto& capture_name : captures) {
        auto capture_info_opt = ctx.get_symbol_info(capture_name);
        if (capture_info_opt) {
            const auto& capture_info = capture_info_opt.value();
            llvm::Value* capture_value = nullptr;
            
            // Extrair o valor primitivo usando o runtime para garantir independência total
            auto* I32 = llvm::Type::getInt32Ty(llvm_context);
            auto* I8P = llvm::PointerType::getUnqual(llvm_context);
            
            // Criar um Value temporário para extração
            auto* temp_alloca = ctx.create_alloca(ValueTy, capture_name + "_temp");
            
            if (capture_info.is_allocated) {
                // É uma alocação - carregar o Value
                auto* loaded_val = builder.CreateLoad(ValueTy, capture_info.value, capture_name + "_loaded");
                builder.CreateStore(loaded_val, temp_alloca);
            } else {
                // Já é um Value direto
                builder.CreateStore(capture_info.value, temp_alloca);
            }
            
            // Extrair o primitivo do Value
            auto* extract_fn = ctx.ensure_runtime_func("extract_int_from_value", {I32, ValuePtr});
            auto* primitive_val = builder.CreateCall(extract_fn, {temp_alloca}, "extracted");
            
            // Criar um Value completamente novo e independente
            auto* final_alloca = ctx.create_alloca(ValueTy, capture_name + "_final");
            auto* create_fn = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
            builder.CreateCall(create_fn, {final_alloca, primitive_val});
            
            // Carregar o Value final
            capture_value = builder.CreateLoad(ValueTy, final_alloca, capture_name + "_capture");
            captured_values.push_back(capture_value);
        } else {
            captured_values.push_back(llvm::UndefValue::get(ValueTy));
        }
    }
    
    // === Gerar corpo da função em separado ===
    auto* saved_function = ctx.get_current_function();
    auto* saved_program_func = ctx.get_program_function();
    auto* saved_builder_insert_point = builder.GetInsertBlock();
    
    // Mudar para o contexto da função da closure
    ctx.set_current_function(closure_func);
    ctx.set_program_function(closure_func);
    
    // Manter o escopo atual para evitar segmentation fault
    
    // Criar entry block
    auto* entry_block = llvm::BasicBlock::Create(llvm_context, "entry", closure_func);
    builder.SetInsertPoint(entry_block);
    
    // Configurar argumentos
    auto args_iter = closure_func->arg_begin();
    llvm::Value* args_ptr = &*args_iter++;  // Value* args
    args_iter->setName("argc");
    llvm::Value* argc_val = &*args_iter++; // int argc
    args_iter->setName("captures");
    llvm::Value* captures_ptr = &*args_iter++; // Value* captures
    args_iter->setName("capture_count");
    llvm::Value* capture_count_val = &*args_iter++; // int capture_count
    
    // Entrar no escopo da closure
    ctx.enter_scope();
    
    // Extrair e registrar parâmetros da closure
    for (size_t i = 0; i < parameters.size(); ++i) {
        const auto& param = parameters[i];
        const std::string& param_name = param.first;
        
        // Carregar argumento do array args
        auto* index = llvm::ConstantInt::get(I32, i);
        auto* arg_ptr = builder.CreateGEP(ValueTy, args_ptr, {index}, "arg_ptr");
        auto* param_value = builder.CreateLoad(ValueTy, arg_ptr, param_name);
        
        // Criar alloca para o parâmetro e registrar
        auto* param_alloca = ctx.create_alloca(ValueTy, param_name);
        builder.CreateStore(param_value, param_alloca);
        
        // Registrar na tabela de símbolos
        auto param_type = ctx.get_type_checker() ? 
            static_cast<nv::Checker*>(ctx.get_type_checker())->gettyptr(param.second) :
            nullptr;
        nv::SymbolInfo info(param_alloca, ValueTy, param_type, true, false);
        ctx.get_symbol_table().define_symbol(param_name, info);
    }
    
    // Registrar variáveis capturadas (se houver)
    // Para closures aninhadas, precisamos garantir que as variáveis capturadas
    // sejam registradas ANTES de gerar o corpo para que os IdentifierNodes as encontrem
    for (size_t i = 0; i < captures.size(); ++i) {
        const std::string& capture_name = captures[i];
        
        // Carregar do array de capturas
        auto* index = llvm::ConstantInt::get(I32, i);
        auto* capture_gep = builder.CreateGEP(ValueTy, captures_ptr, {index}, "capture_ptr");
        auto* capture_value = builder.CreateLoad(ValueTy, capture_gep, capture_name);
        
        // Criar alloca para a captura e registrar
        auto* capture_alloca = ctx.create_alloca(ValueTy, capture_name);
        builder.CreateStore(capture_value, capture_alloca);
        
        // IMPORTANTE: Registrar na tabela de símbolos do escopo ATUAL da closure
        // Isso sobrescreve qualquer referência externa
        nv::SymbolInfo info(capture_alloca, ValueTy, nullptr, true, false);
        ctx.get_symbol_table().define_symbol(capture_name, info);
    }
    
    // Gerar código do corpo da closure
    llvm::Value* return_value = nullptr;
    bool has_explicit_return = false;
    
    for (size_t i = 0; i < body.size(); ++i) {
        auto& stmt = body[i];
        if (stmt) {
            // Verificar se é um return statement
            if (stmt->kind == NodeType::ReturnStatement) {
                has_explicit_return = true;
            }
            
            stmt->codegen(ctx);
            
            // Se tiver valor na pilha, consumir (exceto se for o último)
            if (ctx.has_value() && (i != body.size() - 1)) {
                ctx.pop_value();
            }
        }
    }
    
    // === Obter valor de retorno ===
    // Se houver um return explícito, o ReturnStmtNode já criou o ret instruction
    if (has_explicit_return) {
        // Não fazer nada, o return já foi feito
    } else if (ctx.has_value()) {
        return_value = ctx.pop_value();
        // Garantir que o valor de retorno seja do tipo correto (Value)
        if (return_value->getType() != ValueTy) {
            // Se não for Value, precisa ser convertido
            auto* temp_alloca = ctx.create_alloca(ValueTy, "ret_temp");
            
            auto* I32 = llvm::Type::getInt32Ty(llvm_context);
            auto* F64 = llvm::Type::getDoubleTy(llvm_context);
            
            if (return_value->getType()->isIntegerTy(32)) {
                auto* create_int_fn = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
                builder.CreateCall(create_int_fn, {temp_alloca, return_value});
            } else if (return_value->getType()->isFloatingPointTy()) {
                auto* create_float_fn = ctx.ensure_runtime_func("create_float", {ValuePtr, F64});
                builder.CreateCall(create_float_fn, {temp_alloca, return_value});
            } else {
                // Outros tipos - armazenar como indefinido
                builder.CreateStore(llvm::UndefValue::get(ValueTy), temp_alloca);
            }
            
            return_value = builder.CreateLoad(ValueTy, temp_alloca, "ret_value");
        }
        
        // Retornar da função
        builder.CreateRet(return_value);
    } else {
        // Se não houver retorno explícito, retornar void como Value indefinido
        return_value = llvm::UndefValue::get(ValueTy);
        builder.CreateRet(return_value);
    }
    
    // Sair do escopo da closure
    ctx.exit_scope();
    
    // === Restaurar contexto original ===
    ctx.set_current_function(saved_function);
    ctx.set_program_function(saved_program_func);
    builder.SetInsertPoint(saved_builder_insert_point);
    
    // === Criar objeto closure no escopo original ===
    auto* closure_alloca = ctx.create_alloca(ValueTy, "closure_obj");
    
    if (captures.empty()) {
        // Closure sem capturas
        auto* create_fn = ctx.ensure_runtime_func("create_closure", {ValuePtr, I8P});
        auto* func_ptr = builder.CreateBitCast(closure_func, I8P, "func_ptr");
        builder.CreateCall(create_fn, {closure_alloca, func_ptr});
    } else {
        // Closure com capturas - preparar array de capturas
        auto* captures_array = builder.CreateAlloca(ValueTy, 
            llvm::ConstantInt::get(I32, captures.size()), "captures_array");
        
        // Preencher array com valores capturados coletados anteriormente
        for (size_t i = 0; i < captured_values.size(); ++i) {
            auto* index = llvm::ConstantInt::get(I32, i);
            auto* elem_ptr = builder.CreateGEP(ValueTy, captures_array, {index});
            builder.CreateStore(captured_values[i], elem_ptr);
        }
        
        // Criar closure com capturas
        auto* create_fn = ctx.ensure_runtime_func("create_closure_with_captures", 
            {ValuePtr, I8P, ValuePtr, I32});
        auto* func_ptr = builder.CreateBitCast(closure_func, I8P, "func_ptr");
        auto* captures_count = llvm::ConstantInt::get(I32, captures.size());
        builder.CreateCall(create_fn, {closure_alloca, func_ptr, captures_array, captures_count});
    }
    
    // Carregar e retornar o objeto closure
    auto* closure_value = builder.CreateLoad(ValueTy, closure_alloca, "closure");
    ctx.push_value(closure_value);
}
