#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/access_expr_node.hpp"

void AssignmentExprNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    if (!target) { ctx.push_value(nullptr); return; }
    // Suporte a atribuição para access expressions: base[index] = value
    if (auto* acc = dynamic_cast<AccessExprNode*>(target.get())) {
        // Gera RHS primeiro (preservaremos para retorno)
        if (value) value->codegen(ctx);
        llvm::Value* rhs = ctx.pop_value();
        if (!rhs) { ctx.push_value(nullptr); return; }

        // Avalia base e índice
        if (acc->expr) acc->expr->codegen(ctx);
        llvm::Value* base = ctx.pop_value();
        if (acc->index) acc->index->codegen(ctx);
        llvm::Value* idx_v = ctx.pop_value();

        auto& B = ctx.get_builder();
        auto& C = ctx.get_context();
        auto& M = ctx.get_module();
        auto* ValueTy = nv::ir_utils::get_value_struct(ctx);
        auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
        auto* I32 = llvm::Type::getInt32Ty(C);

        // Coagir índice para i32
        if (idx_v && idx_v->getType() != I32) idx_v = nv::ir_utils::promote_type(ctx, idx_v, I32);
        if (!idx_v) idx_v = llvm::ConstantInt::get(I32, 0);

        // Preparar self (Value*)
        if (!base || base->getType() != ValueTy) { ctx.push_value(nullptr); return; }
        auto* selfAlloca = ctx.create_alloca(ValueTy, "idx.self");
        B.CreateStore(base, selfAlloca);

        // Box do RHS para Value*
        auto box_arg = [&](llvm::Value* any) -> llvm::Value* {
            auto* tmp = ctx.create_alloca(ValueTy, "set.val");
            if (any->getType() == ValueTy) {
                B.CreateStore(any, tmp);
            } else if (any->getType()->isIntegerTy(1)) {
                auto decl = M.getOrInsertFunction("create_bool", llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, I32}, false));
                B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp, B.CreateZExt(any, I32)});
            } else if (any->getType()->isIntegerTy()) {
                auto decl = M.getOrInsertFunction("create_int", llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, I32}, false));
                llvm::Value* iv = any->getType()->isIntegerTy(32) ? any : B.CreateSExtOrTrunc(any, I32);
                B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp, iv});
            } else if (any->getType()->isFloatingPointTy()) {
                auto* F64 = llvm::Type::getDoubleTy(C);
                llvm::Value* fp = any;
                if (any->getType() != F64) fp = B.CreateFPExt(any, F64);
                auto decl = M.getOrInsertFunction("create_float", llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, F64}, false));
                B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp, fp});
            } else if (any->getType() == nv::ir_utils::get_i8_ptr(ctx)) {
                auto decl = M.getOrInsertFunction("create_str", llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, nv::ir_utils::get_i8_ptr(ctx)}, false));
                B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp, any});
            } else {
                B.CreateStore(llvm::UndefValue::get(ValueTy), tmp);
            }
            return tmp;
        };

        auto* rhsBox = box_arg(rhs);

        // Despacho por tag: TAG_ARRAY (5) ou TAG_VECTOR (6)
        auto selfVal = B.CreateLoad(ValueTy, selfAlloca);
        auto tag = B.CreateExtractValue(selfVal, {0});
        auto* I1 = llvm::Type::getInt1Ty(C);
        auto* isArray = B.CreateICmpEQ(tag, llvm::ConstantInt::get(tag->getType(), 5));
        auto* isVector = B.CreateICmpEQ(tag, llvm::ConstantInt::get(tag->getType(), 6));

        auto* curFn = ctx.get_current_function();
        auto* bbArr = llvm::BasicBlock::Create(C, "idx.set.array", curFn);
        auto* bbVec = llvm::BasicBlock::Create(C, "idx.set.vector", curFn);
        auto* bbMerge = llvm::BasicBlock::Create(C, "idx.set.merge", curFn);

        // Branch: prefer array if tag==5, else if tag==6 go vector, else merge
        auto* condArr = isArray;
        B.CreateCondBr(condArr, bbArr, bbVec);

        // Array path
        B.SetInsertPoint(bbArr);
        {
            auto& M_ref = ctx.get_module();
            auto decl = M_ref.getOrInsertFunction(
                "array_set_index_v",
                llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, I32, ValuePtr}, false)
            );
            B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {selfAlloca, idx_v, rhsBox});
            B.CreateBr(bbMerge);
        }

        // Vector path
        B.SetInsertPoint(bbVec);
        {
            auto& M_ref = ctx.get_module();
            auto decl = M_ref.getOrInsertFunction(
                "vector_set_method",
                llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, I32, ValuePtr}, false)
            );
            B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {selfAlloca, idx_v, rhsBox});
            B.CreateBr(bbMerge);
        }

        // Merge
        B.SetInsertPoint(bbMerge);
        ctx.push_value(rhs);
        return;
    }

    auto* id = dynamic_cast<IdentifierNode*>(target.get());
    if (!id) { ctx.push_value(nullptr); return; } // por enquanto só suportamos atribuição a identificadores

    if (value) value->codegen(ctx);
    llvm::Value* rhs = ctx.pop_value();
    if (!rhs) { ctx.push_value(nullptr); return; }

    bool is_simple_assign = (op.empty() || op == "=");
    std::string bin_op;
    if (!is_simple_assign) {
        if (op == "+=") bin_op = "+";
        else if (op == "-=") bin_op = "-";
        else if (op == "*=") bin_op = "*";
        else if (op == "/=") bin_op = "/";
        else if (op == "//=") bin_op = "//";
        else if (op == "**=") bin_op = "**";
        else if (op == "%=") bin_op = "%";
        else is_simple_assign = true; 
    }

    auto info_opt = ctx.get_symbol_table().lookup_symbol(id->symbol);
    if (info_opt.has_value()) {
        auto info = info_opt.value();
        auto& B = ctx.get_builder();
        auto& M = ctx.get_module();
        auto* ValueTy = nv::ir_utils::get_value_struct(ctx);
        auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
        auto& C = ctx.get_context();
        auto* I32 = llvm::Type::getInt32Ty(C);
        auto* F64 = llvm::Type::getDoubleTy(C);

        // Atribuição simples: se a variável é ValueTy, embrulhar o valor primitivo
        if (is_simple_assign) {
            // Se a variável é do tipo Value, embrulhar o valor primitivo antes de armazenar
            if (info.llvm_type == ValueTy && rhs->getType() != ValueTy) {
                auto* tmp_alloca = ctx.create_alloca(ValueTy, id->symbol + "_assign");
                
                // Embrulhar valor primitivo em Value struct
                if (rhs->getType()->isIntegerTy(1)) {
                    auto decl = M.getOrInsertFunction("create_bool", llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, I32}, false));
                    B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp_alloca, B.CreateZExt(rhs, I32)});
                } else if (rhs->getType()->isIntegerTy()) {
                    auto decl = M.getOrInsertFunction("create_int", llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, I32}, false));
                    llvm::Value* iv = rhs->getType()->isIntegerTy(32) ? rhs : B.CreateSExtOrTrunc(rhs, I32);
                    B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp_alloca, iv});
                } else if (rhs->getType()->isFloatingPointTy()) {
                    auto decl = M.getOrInsertFunction("create_float", llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, F64}, false));
                    llvm::Value* fp = rhs->getType() == F64 ? rhs : B.CreateFPExt(rhs, F64);
                    B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp_alloca, fp});
                } else if (rhs->getType() == nv::ir_utils::get_i8_ptr(ctx)) {
                    auto decl = M.getOrInsertFunction("create_str", llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, nv::ir_utils::get_i8_ptr(ctx)}, false));
                    B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp_alloca, rhs});
                } else {
                    B.CreateStore(llvm::UndefValue::get(ValueTy), tmp_alloca);
                }
                
                auto* boxed = B.CreateLoad(ValueTy, tmp_alloca);
                B.CreateStore(boxed, info.value);
                ctx.push_value(boxed);
            } else {
                // Comportamento original para tipos não-Value
                rhs = nv::ir_utils::promote_type(ctx, rhs, info.llvm_type);
                B.CreateStore(rhs, info.value);
                ctx.push_value(rhs);
            }
            return;
        }

        // Atribuições compostas: carrega o valor atual e aplica o operador binário
        llvm::Value* current = B.CreateLoad(info.llvm_type, info.value);
        rhs = nv::ir_utils::promote_type(ctx, rhs, info.llvm_type);
        llvm::Value* result = nv::ir_utils::create_binary_op(ctx, current, rhs, bin_op);
        if (!result) { ctx.push_value(nullptr); return; }

        // Garantir que o resultado tenha o tipo da variável
        result = nv::ir_utils::promote_type(ctx, result, info.llvm_type);
        B.CreateStore(result, info.value);
        ctx.push_value(result);
        return;
    }

    // fallback: verificar se já existe uma variável global ou criar nova
    bool is_global = (ctx.get_current_function() == nullptr);
    auto* ValueTy = nv::ir_utils::get_value_struct(ctx);
    llvm::Type* chosenTy = nullptr;
    llvm::Value* storage = nullptr;
    
    if (is_global) {
        // Variável global: sempre usar Value struct
        chosenTy = ValueTy;
        
        auto& M = ctx.get_module();
        auto& B = ctx.get_builder();
        auto& C = ctx.get_context();
        auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
        auto* I32 = llvm::Type::getInt32Ty(C);
        auto* F64 = llvm::Type::getDoubleTy(C);
        
        // Verificar se já existe um GlobalVariable com esse nome (pode ter sido criado por import)
        llvm::GlobalVariable* global = M.getGlobalVariable(id->symbol);
        
        if (!global) {
            // Criar variável global inicializada como zero
            global = new llvm::GlobalVariable(
                M, ValueTy, false,
                llvm::GlobalValue::InternalLinkage,  // linkage interno (módulos combinados)
                llvm::Constant::getNullValue(ValueTy),  // inicializar como zero
                id->symbol
            );
        }
        
        // Inicializar diretamente no GlobalVariable usando um ponteiro para ele
        // Criar um ponteiro para o GlobalVariable (Value*)
        auto* global_ptr = B.CreateBitCast(global, ValuePtr);
        
        // Embrulhar valor primitivo em Value struct diretamente no GlobalVariable
        if (rhs->getType() == ValueTy) {
            // Já é Value, apenas copiar diretamente
            B.CreateStore(rhs, global);
            rhs = B.CreateLoad(ValueTy, global);  // Para retornar o valor
        } else if (rhs->getType()->isIntegerTy(1)) {
            auto* f = ctx.ensure_runtime_func("create_bool", {ValuePtr, I32});
            B.CreateCall(f, {global_ptr, B.CreateZExt(rhs, I32)});
            rhs = B.CreateLoad(ValueTy, global);  // Para retornar o valor embrulhado
        } else if (rhs->getType()->isIntegerTy()) {
            auto* f = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
            llvm::Value* iv = rhs->getType()->isIntegerTy(32) ? rhs : B.CreateSExtOrTrunc(rhs, I32);
            B.CreateCall(f, {global_ptr, iv});
            rhs = B.CreateLoad(ValueTy, global);  // Para retornar o valor embrulhado
        } else if (rhs->getType()->isFloatingPointTy()) {
            auto* f = ctx.ensure_runtime_func("create_float", {ValuePtr, F64});
            llvm::Value* fp = rhs->getType() == F64 ? rhs : B.CreateFPExt(rhs, F64);
            B.CreateCall(f, {global_ptr, fp});
            rhs = B.CreateLoad(ValueTy, global);  // Para retornar o valor embrulhado
        } else if (rhs->getType() == nv::ir_utils::get_i8_ptr(ctx)) {
            auto* f = ctx.ensure_runtime_func("create_str", {ValuePtr, nv::ir_utils::get_i8_ptr(ctx)});
            B.CreateCall(f, {global_ptr, rhs});
            rhs = B.CreateLoad(ValueTy, global);  // Para retornar o valor embrulhado
        } else {
            B.CreateStore(llvm::UndefValue::get(ValueTy), global);
            rhs = B.CreateLoad(ValueTy, global);  // Para retornar o valor
        }
        
        storage = global;
        
        // Garantir que o GlobalVariable está registrado na tabela de símbolos
        // (pode ter sido criado por import mas não registrado ainda, ou vice-versa)
        auto existing_info = ctx.get_symbol_table().lookup_symbol(id->symbol);
        if (!existing_info.has_value() || existing_info.value().value != global) {
            // Registrar na tabela de símbolos se não estiver ou se for diferente
            nv::SymbolInfo info(
                global,
                chosenTy,
                nullptr,
                false,  // não é alocação local
                false   // não é constante
            );
            ctx.get_symbol_table().define_symbol(id->symbol, info);
        }
    } else {
        // Variável local: comportamento original
        if (rhs->getType() == ValueTy) {
            chosenTy = ValueTy;
        } else {
            chosenTy = llvm::Type::getInt32Ty(ctx.get_context());
            rhs = nv::ir_utils::promote_type(ctx, rhs, chosenTy);
        }
        storage = ctx.create_and_register_variable(id->symbol, chosenTy, nullptr, false);
        ctx.get_builder().CreateStore(rhs, storage);
        
        // Para variáveis locais, o registro já foi feito por create_and_register_variable
    }
    
    // Para variáveis globais, o registro já foi feito acima quando verificamos se já existia
    // Para variáveis locais, o registro já foi feito por create_and_register_variable
    ctx.push_value(rhs);
}