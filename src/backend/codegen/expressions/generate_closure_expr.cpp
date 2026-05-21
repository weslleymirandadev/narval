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
    
    // Captures are stored as Value* slots. Locals/params that would otherwise
    // die with the current stack frame are promoted to heap cells when captured.
    std::vector<llvm::Value*> captured_slots;
    auto* capture_cell_new = ctx.ensure_runtime_func("nv_closure_cell_new", {ValuePtr}, ValuePtr);

    auto box_to_value_slot = [&](llvm::Value* value, const std::string& name) -> llvm::Value* {
        auto* boxed = ctx.create_alloca(ValueTy, name + "_boxed");
        if (!value) {
            builder.CreateStore(llvm::UndefValue::get(ValueTy), boxed);
        } else if (value->getType() == ValueTy) {
            builder.CreateStore(value, boxed);
        } else if (value->getType()->isIntegerTy(1)) {
            auto* create_bool_fn = ctx.ensure_runtime_func("create_bool", {ValuePtr, I32});
            builder.CreateCall(create_bool_fn, {boxed, builder.CreateZExt(value, I32)});
        } else if (value->getType()->isIntegerTy()) {
            auto* create_int_fn = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
            llvm::Value* iv = value->getType()->isIntegerTy(32)
                ? value
                : builder.CreateSExtOrTrunc(value, I32);
            builder.CreateCall(create_int_fn, {boxed, iv});
        } else if (value->getType()->isFloatingPointTy()) {
            auto* F64 = llvm::Type::getDoubleTy(llvm_context);
            auto* create_float_fn = ctx.ensure_runtime_func("create_float", {ValuePtr, F64});
            llvm::Value* fv = value->getType() == F64 ? value : builder.CreateFPExt(value, F64);
            builder.CreateCall(create_float_fn, {boxed, fv});
        } else if (value->getType() == I8P) {
            auto* create_str_fn = ctx.ensure_runtime_func("create_str", {ValuePtr, I8P});
            builder.CreateCall(create_str_fn, {boxed, value});
        } else {
            builder.CreateStore(llvm::UndefValue::get(ValueTy), boxed);
        }
        return boxed;
    };

    for (const auto& capture_name : captures) {
        auto capture_info_opt = ctx.get_symbol_info(capture_name);
        if (capture_info_opt) {
            const auto& capture_info = capture_info_opt.value();
            llvm::Value* capture_slot = nullptr;

            if (llvm::isa<llvm::GlobalVariable>(capture_info.value)) {
                capture_slot = capture_info.value;
            } else if (capture_info.is_allocated &&
                       !llvm::isa<llvm::AllocaInst>(capture_info.value) &&
                       capture_info.value->getType() == ValuePtr) {
                // Already a heap/environment slot from an outer closure.
                capture_slot = capture_info.value;
            } else if (capture_info.is_allocated) {
                llvm::Value* loaded = builder.CreateLoad(
                    capture_info.llvm_type, capture_info.value, capture_name + "_loaded");
                auto* boxed = box_to_value_slot(loaded, capture_name);
                capture_slot = builder.CreateCall(capture_cell_new, {boxed}, capture_name + "_cell");

                nv::SymbolInfo promoted(capture_slot, ValueTy, capture_info.nv_type, true, false);
                if (!ctx.get_symbol_table().update_symbol(capture_name, promoted)) {
                    ctx.get_symbol_table().define_symbol(capture_name, promoted);
                }
            } else if (capture_info.value && capture_info.value->getType() == ValuePtr) {
                capture_slot = capture_info.value;
            } else {
                auto* boxed = box_to_value_slot(capture_info.value, capture_name);
                capture_slot = builder.CreateCall(capture_cell_new, {boxed}, capture_name + "_cell");
            }

            captured_slots.push_back(capture_slot);
        } else {
            auto* boxed = box_to_value_slot(nullptr, capture_name);
            captured_slots.push_back(builder.CreateCall(capture_cell_new, {boxed}, capture_name + "_missing_cell"));
        }
    }
    
    // === Gerar corpo da função em separado ===
    auto* saved_function = ctx.get_current_function();
    auto* saved_program_func = ctx.get_program_function();
    auto* saved_builder_insert_point = builder.GetInsertBlock();
    
    // Mudar para o contexto da função da closure
    ctx.set_current_function(closure_func);
    
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

    // Re-registrar type params (<T, E>) no checker durante o codegen.
    // O checker os remove após a fase de checking para evitar vazamento, mas o codegen
    // ainda os precisa para resolver nomes de tipo nos parâmetros/retorno.
    nv::Checker* checker_ptr = ctx.get_type_checker()
        ? static_cast<nv::Checker*>(ctx.get_type_checker()) : nullptr;
    std::vector<std::pair<std::string, std::shared_ptr<nv::Type>>> saved_codegen_tp;
    if (checker_ptr) {
        for (const auto& tp_name : type_params) {
            auto prev_it = checker_ptr->types.find(tp_name);
            saved_codegen_tp.push_back({tp_name,
                prev_it != checker_ptr->types.end() ? prev_it->second : nullptr});
            checker_ptr->types[tp_name] = checker_ptr->unify_ctx.new_type_var();
        }
    }

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
        auto param_type = checker_ptr ? checker_ptr->gettyptr(param.second) : nullptr;
        nv::SymbolInfo info(param_alloca, ValueTy, param_type, true, false);
        ctx.get_symbol_table().define_symbol(param_name, info);
    }
    
    // Registrar variáveis capturadas (se houver)
    // Para closures aninhadas, precisamos garantir que as variáveis capturadas
    // sejam registradas ANTES de gerar o corpo para que os IdentifierNodes as encontrem
    for (size_t i = 0; i < captures.size(); ++i) {
        const std::string& capture_name = captures[i];
        
        // Load the captured Value* slot and register it directly. Assignments
        // to the captured name store through this slot, preserving JS-like state.
        auto* index = llvm::ConstantInt::get(I32, i);
        auto* capture_gep = builder.CreateGEP(ValuePtr, captures_ptr, {index}, "capture_slot_ptr");
        auto* capture_slot = builder.CreateLoad(ValuePtr, capture_gep, capture_name + "_slot");
        
        // IMPORTANTE: Registrar na tabela de símbolos do escopo ATUAL da closure
        // Isso sobrescreve qualquer referência externa
        nv::SymbolInfo info(capture_slot, ValueTy, nullptr, true, false);
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
        auto* captures_array = builder.CreateAlloca(ValuePtr,
            llvm::ConstantInt::get(I32, captures.size()), "captures_array");
        
        // Preencher array com slots capturados coletados anteriormente
        for (size_t i = 0; i < captured_slots.size(); ++i) {
            auto* index = llvm::ConstantInt::get(I32, i);
            auto* elem_ptr = builder.CreateGEP(ValuePtr, captures_array, {index});
            builder.CreateStore(captured_slots[i], elem_ptr);
        }
        
        // Criar closure com capturas
        auto* create_fn = ctx.ensure_runtime_func("create_closure_with_captures", 
            {ValuePtr, I8P, ValuePtr, I32});
        auto* func_ptr = builder.CreateBitCast(closure_func, I8P, "func_ptr");
        auto* captures_count = llvm::ConstantInt::get(I32, captures.size());
        builder.CreateCall(create_fn, {closure_alloca, func_ptr, captures_array, captures_count});
    }
    
    // Restaurar type params no checker (remove os que não existiam antes)
    if (checker_ptr) {
        for (const auto& [name, prev] : saved_codegen_tp) {
            if (prev) checker_ptr->types[name] = prev;
            else      checker_ptr->types.erase(name);
        }
    }

    // Carregar e retornar o objeto closure
    auto* closure_value = builder.CreateLoad(ValueTy, closure_alloca, "closure");
    ctx.push_value(closure_value);
}
