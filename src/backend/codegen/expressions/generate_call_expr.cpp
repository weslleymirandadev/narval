#include "frontend/ast/expressions/call_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"

namespace {

using namespace nv;

struct MethodInfo {
    const char* name;
    int arity;
    const char* rt_func;
    bool returns_value;
};

const MethodInfo string_methods[] = {
    {"toUpperCase", 0, "string_to_upper_case", true},
    {"replace",     2, "string_replace",       true},
    {"includes",    1, "string_includes",      true},
    {nullptr}
};

const MethodInfo vector_methods[] = {
    {"push", 1, "vector_push_method", false},
    {"pop",  0, "vector_pop_method",  true},
    {"get",  1, "vector_get_method",  true},
    {"set",  2, "vector_set_method",  false},
    {nullptr}
};

// === HELPER: Box any value to Value* ===
llvm::Value* box_value(IRGenerationContext& ctx, llvm::Value* v) {
    auto& B = ctx.get_builder();
    auto* ValueTy = ir_utils::get_value_struct(ctx);
    auto* alloca = B.CreateAlloca(ValueTy, nullptr, "boxed");

    // Handle null values gracefully
    if (!v) {
        B.CreateStore(llvm::UndefValue::get(ValueTy), alloca);
        return alloca;
    }

    if (v->getType() == ValueTy) {
        B.CreateStore(v, alloca);
        return alloca;
    }

    auto* I32 = llvm::Type::getInt32Ty(ctx.get_context());
    auto* F64 = llvm::Type::getDoubleTy(ctx.get_context());

    if (v->getType()->isIntegerTy(1)) {
        auto* f = ctx.ensure_runtime_func("create_bool", {ir_utils::get_value_ptr(ctx), I32});
        B.CreateCall(f, {alloca, B.CreateZExt(v, I32)});
    }
    else if (v->getType()->isIntegerTy()) {
        auto* f = ctx.ensure_runtime_func("create_int", {ir_utils::get_value_ptr(ctx), I32});
        llvm::Value* iv = v->getType()->isIntegerTy(32) ? v : B.CreateSExtOrTrunc(v, I32);
        B.CreateCall(f, {alloca, iv});
    }
    else if (v->getType()->isFloatingPointTy()) {
        auto* f = ctx.ensure_runtime_func("create_float", {ir_utils::get_value_ptr(ctx), F64});
        llvm::Value* fv = v->getType() == F64 ? v : B.CreateFPExt(v, F64);
        B.CreateCall(f, {alloca, fv});
    }
    else if (v->getType()->isPointerTy()) {
        auto* I8P = nv::ir_utils::get_i8_ptr(ctx);
        llvm::Value* s = (v->getType() == I8P) ? v : B.CreateBitCast(v, I8P);
        auto* f = ctx.ensure_runtime_func("create_str", {ir_utils::get_value_ptr(ctx), I8P});
        B.CreateCall(f, {alloca, s});
    }
    else {
        B.CreateStore(llvm::UndefValue::get(ValueTy), alloca);
    }
    return alloca;
}

// === HELPER: Emit nv_write ===
void emit_write(IRGenerationContext& ctx, llvm::Value* v, bool newline = true) {
    // If value generation failed, emit undef Value
    if (!v) {
        v = llvm::UndefValue::get(ir_utils::get_value_struct(ctx));
    }
    
    auto* ValueTy = ir_utils::get_value_struct(ctx);
    auto* ValuePtr = ir_utils::get_value_ptr(ctx);
    llvm::Value* boxed = nullptr;
    
    // Se já é Value struct, apenas criar alloca e passar ponteiro
    if (v->getType() == ValueTy) {
        auto* alloca = ctx.get_builder().CreateAlloca(ValueTy, nullptr, "write_val");
        ctx.get_builder().CreateStore(v, alloca);
        boxed = alloca;
    } else {
        // Caso contrário, embrulhar usando box_value
        boxed = box_value(ctx, v);
    }
    
    const char* name = newline ? "nv_write" : "nv_write_no_nl";
    auto* fn = ctx.ensure_runtime_func(name, {ValuePtr});
    ctx.get_builder().CreateCall(fn, {boxed});
}

// === BUILTIN: write, read, json.load ===
llvm::Value* try_lower_builtin(IRGenerationContext& ctx, const std::string& name, const std::vector<std::unique_ptr<Expr>>& args) {
    auto& B = ctx.get_builder();
    auto* I8P = ir_utils::get_i8_ptr(ctx);

    if (name == "write") {
        if (args.empty()) {
            auto* empty = B.CreateGlobalStringPtr("");
            emit_write(ctx, empty);
            // Return an undef Value for empty write
            return llvm::UndefValue::get(ir_utils::get_value_struct(ctx));
        } else {
            // Generate code for the argument
            args[0]->codegen(ctx);
            // Get the value but keep it on the stack conceptually
            llvm::Value* val = ctx.pop_value();
            
            // Emit write with the value (this will box it internally)
            emit_write(ctx, val);
            
            // Return the original value so it can be used as program return value
            // IMPORTANT: We need to preserve the value by creating a copy in an alloca
            // This ensures the value survives optimization and is available for program return
            auto* ValueTy = ir_utils::get_value_struct(ctx);
            if (val->getType() == ValueTy) {
                // Create a copy in an alloca to preserve the value
                auto* preserve_alloca = B.CreateAlloca(ValueTy, nullptr, "write_result");
                B.CreateStore(val, preserve_alloca);
                // Load it back to ensure it's preserved
                return B.CreateLoad(ValueTy, preserve_alloca, "write_preserved");
            } else {
                // Box the value to return it as a Value struct
                llvm::Value* boxed = box_value(ctx, val);
                return B.CreateLoad(ValueTy, boxed);
            }
        }
    }

    if (name == "read") {
        if (!args.empty()) {
            args[0]->codegen(ctx);
            emit_write(ctx, ctx.pop_value(), false);
        }
        auto* fn = ctx.ensure_runtime_func("nv_read", {}, I8P);
        return B.CreateCall(fn, {});
    }
    
    return nullptr; // not builtin
}

llvm::Value* lower_method_call(IRGenerationContext& ctx, llvm::Value* selfAlloca, const std::string& method, const std::vector<llvm::Value*>& argv) {
    auto& B = ctx.get_builder();
    auto* ValueTy = ir_utils::get_value_struct(ctx);
    auto* ValuePtr = ir_utils::get_value_ptr(ctx);

    // Load self to get prototype
    llvm::Value* selfVal = B.CreateLoad(ValueTy, selfAlloca);
    llvm::Value* proto = B.CreateExtractValue(selfVal, 2);

    // String methods (explicit signatures)
    if (method == "toUpperCase") {
        auto* fn = ctx.ensure_runtime_func("string_to_upper_case", {ValuePtr, ValuePtr});
        auto* out = B.CreateAlloca(ValueTy, nullptr, "out");
        B.CreateCall(fn, {out, selfAlloca});
        return B.CreateLoad(ValueTy, out);
    } else if (method == "replace") {
        if (argv.size() < 2) return nullptr;
        auto* fn = ctx.ensure_runtime_func("string_replace", {ValuePtr, ValuePtr, ValuePtr, ValuePtr});
        auto* out = B.CreateAlloca(ValueTy, nullptr, "out");
        llvm::Value* a0 = box_value(ctx, argv[0]);
        llvm::Value* a1 = box_value(ctx, argv[1]);
        B.CreateCall(fn, {out, selfAlloca, a0, a1});
        return B.CreateLoad(ValueTy, out);
    } else if (method == "includes") {
        if (argv.empty()) return nullptr;
        auto* fn = ctx.ensure_runtime_func("string_includes", {ValuePtr, ValuePtr, ValueTy});
        auto* out = B.CreateAlloca(ValueTy, nullptr, "out");
        llvm::Value* v = B.CreateLoad(ValueTy, box_value(ctx, argv[0]));
        B.CreateCall(fn, {out, selfAlloca, v});
        return B.CreateLoad(ValueTy, out);
    }

    // Vector methods (explicit signatures)
    if (method == "push") {
        if (argv.empty()) return nullptr;
        auto* fn = ctx.ensure_runtime_func("vector_push_method", {ValuePtr, ValuePtr, ValuePtr});
        auto* out = B.CreateAlloca(ValueTy, nullptr, "out");
        llvm::Value* valPtr = box_value(ctx, argv[0]);
        B.CreateCall(fn, {out, selfAlloca, valPtr});
        return B.CreateLoad(ValueTy, out);
    } else if (method == "pop") {
        auto* fn = ctx.ensure_runtime_func("vector_pop_method", {ValuePtr, ValuePtr});
        auto* out = B.CreateAlloca(ValueTy, nullptr, "out");
        B.CreateCall(fn, {out, selfAlloca});
        return B.CreateLoad(ValueTy, out);
    } else if (method == "get") {
        if (argv.empty()) return nullptr;
        auto* fn = ctx.ensure_runtime_func("vector_get_method", {ValuePtr, ValuePtr, llvm::Type::getInt32Ty(ctx.get_context())});
        auto* out = B.CreateAlloca(ValueTy, nullptr, "out");
        auto* I32 = llvm::Type::getInt32Ty(ctx.get_context());
        llvm::Value* idx = argv[0]->getType()->isIntegerTy(32) ? argv[0] : B.CreateSExtOrTrunc(argv[0], I32);
        B.CreateCall(fn, {out, selfAlloca, idx});
        return B.CreateLoad(ValueTy, out);
    } else if (method == "set") {
        if (argv.size() < 2) return nullptr;
        auto* fn = ctx.ensure_runtime_func("vector_set_method", {ValuePtr, llvm::Type::getInt32Ty(ctx.get_context()), ValuePtr});
        auto* I32 = llvm::Type::getInt32Ty(ctx.get_context());
        llvm::Value* idx = argv[0]->getType()->isIntegerTy(32) ? argv[0] : B.CreateSExtOrTrunc(argv[0], I32);
        llvm::Value* valPtr = box_value(ctx, argv[1]);
        B.CreateCall(fn, {selfAlloca, idx, valPtr});
        return nullptr;
    }

    return nullptr;
}

} // anonymous namespace

void CallExprNode::codegen(IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    auto& B = ctx.get_builder();

    // === 1. BUILTIN CALLS (write, read, json.load) ===
    if (auto* id = dynamic_cast<IdentifierNode*>(caller.get())) {
        std::string name = id->symbol;
        if (auto* result = try_lower_builtin(ctx, name, args)) {
            ctx.push_value(result);
            return;
        }
    }
    
    // === 2. METHOD CALL: obj.method(...) ===
    if (auto* mem = dynamic_cast<MemberExprNode*>(caller.get())) {
        // Avalia o objeto (ex: "json")
        mem->object->codegen(ctx);
        llvm::Value* obj = ctx.pop_value();

        // Extrai o nome do método (ex: "load")
        std::string method;
        if (auto* id = dynamic_cast<IdentifierNode*>(mem->property.get())) {
            method = id->symbol;
        } else {
            ctx.push_value(nullptr);
            return;
        }

        // === ESPECIAL: json.load("file.json") ===
        if (method == "load") {
            // Verifica se o objeto é o identifier "json"
            if (auto* objId = dynamic_cast<IdentifierNode*>(mem->object.get())) {
                if (objId->symbol == "json") {
                    if (args.empty()) {
                        ctx.push_value(nullptr);
                        return;
                    }

                    // Avalia o argumento (filename)
                    args[0]->codegen(ctx);
                    llvm::Value* filename = ctx.pop_value();
                    auto* I8P = ir_utils::get_i8_ptr(ctx);

                    if (!filename->getType()->isPointerTy()) {
                        filename = ctx.get_builder().CreateGlobalStringPtr(""); // fallback
                    } else if (filename->getType() != I8P) {
                        filename = ctx.get_builder().CreateBitCast(filename, I8P);
                    }

                    // Chama void json_load(Value*, const char*) e carrega o Value
                    auto* ValueTy = ir_utils::get_value_struct(ctx);
                    auto* ValuePtr = ir_utils::get_value_ptr(ctx);
                    auto* fn = ctx.ensure_runtime_func(
                        "json_load",
                        {ValuePtr, I8P},
                        llvm::Type::getVoidTy(ctx.get_context())
                    );
                    auto* outAlloca = ctx.get_builder().CreateAlloca(ValueTy, nullptr, "json_out");
                    ctx.get_builder().CreateCall(fn, {outAlloca, filename});
                    llvm::Value* loaded = ctx.get_builder().CreateLoad(ValueTy, outAlloca);
                    ctx.push_value(loaded);
                    return;
                }
            }
        }

        // === FIM DO ESPECIAL json.load ===

        // === MÉTODOS NORMAIS (push, pop, etc) ===
        auto* selfAlloca = box_value(ctx, obj);

        std::vector<llvm::Value*> argv;
        for (auto& a : args) {
            a->codegen(ctx);
            argv.push_back(ctx.pop_value());
        }

        if (auto* result = lower_method_call(ctx, selfAlloca, method, argv)) {
            ctx.push_value(result);
            return;
        }

        ctx.push_value(llvm::UndefValue::get(ir_utils::get_value_struct(ctx)));
        return;
    }

    // === 3. REGULAR FUNCTION CALL ===
    caller->codegen(ctx);
    llvm::Value* callee = ctx.pop_value();

    // Verificar se callee é válido antes de fazer cast
    if (!callee) {
        ctx.push_value(nullptr);
        return;
    }

    if (auto* F = llvm::dyn_cast<llvm::Function>(callee)) {
        // Obter tipos dos parâmetros da função
        auto* func_ty = F->getFunctionType();
        std::vector<llvm::Type*> param_types(func_ty->param_begin(), func_ty->param_end());
        
        std::vector<llvm::Value*> argv;
        auto* ValueTy = ir_utils::get_value_struct(ctx);
        auto* I32 = llvm::Type::getInt32Ty(ctx.get_context());
        auto* I64 = llvm::Type::getInt64Ty(ctx.get_context());
        auto* F64 = llvm::Type::getDoubleTy(ctx.get_context());
        
        for (size_t i = 0; i < args.size() && i < param_types.size(); ++i) {
            args[i]->codegen(ctx);
            llvm::Value* arg_val = ctx.pop_value();
            
            // Se o argumento é Value struct mas a função espera tipo primitivo, extrair o valor
            if (arg_val && arg_val->getType() == ValueTy && param_types[i] != ValueTy) {
                // Extrair valor primitivo do Value struct
                auto* tmp_alloca = B.CreateAlloca(ValueTy, nullptr, "arg_tmp");
                B.CreateStore(arg_val, tmp_alloca);
                
                // Garantir tipo correto
                auto* ValuePtr = ir_utils::get_value_ptr(ctx);
                auto* ensure_func = ctx.ensure_runtime_func("ensure_value_type", {ValuePtr});
                B.CreateCall(ensure_func, {tmp_alloca});
                
                // Extrair campo value (índice 1) que contém o valor como i64
                auto* valuePtr = B.CreateStructGEP(ValueTy, tmp_alloca, 1);
                auto* value64 = B.CreateLoad(I64, valuePtr, "arg_value64");
                
                // Extrair campo type (índice 0) para verificar o tipo real do valor
                auto* typePtr = B.CreateStructGEP(ValueTy, tmp_alloca, 0);
                auto* type32 = B.CreateLoad(I32, typePtr, "arg_type");
                
                // Constantes para tags de tipo
                auto* TAG_INT = llvm::ConstantInt::get(I32, 1);   // TAG_INT = 1
                auto* TAG_FLOAT = llvm::ConstantInt::get(I32, 2); // TAG_FLOAT = 2
                
                // Obter função atual para criar basic blocks
                auto* current_func = ctx.get_current_function();
                if (!current_func) {
                    // Se não há função atual, usar abordagem simples sem branches
                    if (param_types[i] == I32) {
                        arg_val = B.CreateTrunc(value64, I32, "arg_i32");
                    } else if (param_types[i] == I64) {
                        arg_val = value64;
                    } else if (param_types[i] == F64) {
                        // Converter bits i64 para double (assume que já é float ou converte int)
                        arg_val = B.CreateBitCast(value64, F64, "arg_f64");
                    }
                } else {
                    // Converter para o tipo esperado pela função, respeitando o tipo real do valor
                    if (param_types[i] == I32) {
                        // Se o valor é float, converter para int primeiro
                        auto* is_float = B.CreateICmpEQ(type32, TAG_FLOAT, "is_float");
                        auto* then_bb = llvm::BasicBlock::Create(ctx.get_context(), "convert_float_to_int", current_func);
                        auto* else_bb = llvm::BasicBlock::Create(ctx.get_context(), "use_int", current_func);
                        auto* merge_bb = llvm::BasicBlock::Create(ctx.get_context(), "merge_int", current_func);
                        B.CreateCondBr(is_float, then_bb, else_bb);
                        
                        B.SetInsertPoint(then_bb);
                        auto* float_val = B.CreateBitCast(value64, F64, "float_val");
                        auto* int_from_float = B.CreateFPToSI(float_val, I32, "int_from_float");
                        B.CreateBr(merge_bb);
                        
                        B.SetInsertPoint(else_bb);
                        auto* int_val = B.CreateTrunc(value64, I32, "int_val");
                        B.CreateBr(merge_bb);
                        
                        B.SetInsertPoint(merge_bb);
                        auto* phi = B.CreatePHI(I32, 2, "arg_i32");
                        phi->addIncoming(int_from_float, then_bb);
                        phi->addIncoming(int_val, else_bb);
                        arg_val = phi;
                    } else if (param_types[i] == I64) {
                        // Para i64, manter como está (mas pode precisar de conversão de float)
                        auto* is_float = B.CreateICmpEQ(type32, TAG_FLOAT, "is_float");
                        auto* then_bb = llvm::BasicBlock::Create(ctx.get_context(), "convert_float_to_i64", current_func);
                        auto* else_bb = llvm::BasicBlock::Create(ctx.get_context(), "use_i64", current_func);
                        auto* merge_bb = llvm::BasicBlock::Create(ctx.get_context(), "merge_i64", current_func);
                        B.CreateCondBr(is_float, then_bb, else_bb);
                        
                        B.SetInsertPoint(then_bb);
                        auto* float_val = B.CreateBitCast(value64, F64, "float_val");
                        auto* i64_from_float = B.CreateFPToSI(float_val, I64, "i64_from_float");
                        B.CreateBr(merge_bb);
                        
                        B.SetInsertPoint(else_bb);
                        B.CreateBr(merge_bb);
                        
                        B.SetInsertPoint(merge_bb);
                        auto* phi = B.CreatePHI(I64, 2, "arg_i64");
                        phi->addIncoming(i64_from_float, then_bb);
                        phi->addIncoming(value64, else_bb);
                        arg_val = phi;
                    } else if (param_types[i] == F64) {
                        // Se o valor é int, converter para float primeiro
                        auto* is_int = B.CreateICmpEQ(type32, TAG_INT, "is_int");
                        auto* then_bb = llvm::BasicBlock::Create(ctx.get_context(), "convert_int_to_float", current_func);
                        auto* else_bb = llvm::BasicBlock::Create(ctx.get_context(), "use_float", current_func);
                        auto* merge_bb = llvm::BasicBlock::Create(ctx.get_context(), "merge_float", current_func);
                        B.CreateCondBr(is_int, then_bb, else_bb);
                        
                        B.SetInsertPoint(then_bb);
                        auto* int_val = B.CreateTrunc(value64, I32, "int_val");
                        auto* float_from_int = B.CreateSIToFP(int_val, F64, "float_from_int");
                        B.CreateBr(merge_bb);
                        
                        B.SetInsertPoint(else_bb);
                        // Se já é float, apenas fazer bitcast dos bits
                        auto* float_val = B.CreateBitCast(value64, F64, "float_val");
                        B.CreateBr(merge_bb);
                        
                        B.SetInsertPoint(merge_bb);
                        auto* phi = B.CreatePHI(F64, 2, "arg_f64");
                        phi->addIncoming(float_from_int, then_bb);
                        phi->addIncoming(float_val, else_bb);
                        arg_val = phi;
                    } else {
                        // Tipo não suportado, usar valor original
                    }
                }
            }
            
            argv.push_back(arg_val);
        }
        
        auto* call = B.CreateCall(F, argv);
        // Se a função retorna um tipo primitivo, converter para Value struct para manter consistência
        if (!call->getType()->isVoidTy()) {
            if (call->getType() != ValueTy) {
                // Converter retorno primitivo para Value struct
                auto* ret_alloca = B.CreateAlloca(ValueTy, nullptr, "ret_val");
                auto* ValuePtr = ir_utils::get_value_ptr(ctx);
                
                if (call->getType() == I32) {
                    auto* create_int_func = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
                    B.CreateCall(create_int_func, {ret_alloca, call});
                } else if (call->getType() == I64) {
                    // Para i64, precisamos converter para i32 primeiro (create_int espera i32)
                    auto* i32_val = B.CreateTrunc(call, I32, "ret_i32");
                    auto* create_int_func = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
                    B.CreateCall(create_int_func, {ret_alloca, i32_val});
                } else if (call->getType() == F64) {
                    auto* create_float_func = ctx.ensure_runtime_func("create_float", {ValuePtr, F64});
                    B.CreateCall(create_float_func, {ret_alloca, call});
                } else {
                    // Tipo não suportado, usar UndefValue
                    B.CreateStore(llvm::UndefValue::get(ValueTy), ret_alloca);
                }
                
                auto* ret_value = B.CreateLoad(ValueTy, ret_alloca, "ret_value");
                ctx.push_value(ret_value);
            } else {
                // Já é Value struct, apenas empurrar
                ctx.push_value(call);
            }
        } else {
            // Void return, push nullptr
            ctx.push_value(nullptr);
        }
    } else {
        ctx.push_value(nullptr);
    }
}