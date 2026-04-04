#include "frontend/ast/expressions/call_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "backend/codegen/generate_ir.hpp"
#include "backend/codegen/method_handler.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"

// Forward declarations para handlers
namespace nv {
    class BuiltinHandler;
    class JsonHandler;
}

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
    auto* alloca = ctx.create_alloca(ValueTy, "boxed");

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
        auto* alloca = ctx.create_alloca(ValueTy, "write_val");
        ctx.get_builder().CreateStore(v, alloca);
        boxed = alloca;
    } else {
        // Caso contrário, embrulhar usando box_value
        boxed = box_value(ctx, v);
    }
    
    // If generating code for REPL loop body, store boxed value to out_result for the host to print
    if (ctx.is_repl_loop_mode() && ctx.get_repl_out_result_ptr()) {
        // boxed is an alloca pointer to Value; load and store into out_result
        auto* loaded = ctx.get_builder().CreateLoad(ir_utils::get_value_struct(ctx), boxed);
        ctx.get_builder().CreateStore(loaded, ctx.get_repl_out_result_ptr());
        return;
    }

    const char* name = newline ? "nv_write" : "nv_write_no_nl";
    auto* fn = ctx.ensure_runtime_func(name, {ValuePtr});
    ctx.get_builder().CreateCall(fn, {boxed});
}

// === HELPER: Convert value to i32 for exit code ===
static llvm::Value* value_to_i32(IRGenerationContext& ctx, llvm::Value* v) {
    auto& B = ctx.get_builder();
    auto* I32 = llvm::Type::getInt32Ty(ctx.get_context());
    auto* I64 = llvm::Type::getInt64Ty(ctx.get_context());
    auto* ValueTy = ir_utils::get_value_struct(ctx);
    auto* ValuePtr = ir_utils::get_value_ptr(ctx);
    if (!v) return llvm::ConstantInt::get(I32, 0);
    if (v->getType() == I32) return v;
    if (v->getType()->isIntegerTy()) return B.CreateSExtOrTrunc(v, I32);
    if (v->getType() == ValueTy) {
        auto* tmp = ctx.create_alloca(ValueTy, "exit_code_tmp");
        B.CreateStore(v, tmp);
        auto* ensure_fn = ctx.ensure_runtime_func("ensure_value_type", {ValuePtr});
        B.CreateCall(ensure_fn, {tmp});
        // Para a nova estrutura, usar função runtime para extrair int
        auto* extract_int_func = ctx.ensure_runtime_func("extract_int_from_value", {I32, ValuePtr});
        return B.CreateCall(extract_int_func, {tmp}, "exit_code");
    }
    return llvm::ConstantInt::get(I32, 0);
}

// === BUILTIN: write, read, exit, json.load ===
llvm::Value* try_lower_builtin(IRGenerationContext& ctx, const std::string& name, const std::vector<std::unique_ptr<Expr>>& args) {
    auto& B = ctx.get_builder();
    auto* ValueTy = ir_utils::get_value_struct(ctx);
    auto* I8P = llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(ctx.get_context()));

    // Implementar write diretamente
    if (name == "write") {
        if (args.empty()) {
            auto* empty = B.CreateGlobalStringPtr("");
            emit_write(ctx, empty, true);
            return llvm::UndefValue::get(ValueTy);
        }

        // Gerar código para o argumento
        args[0]->codegen(ctx);
        if (!ctx.has_value()) return nullptr;

        llvm::Value* arg = ctx.pop_value();

        // Sempre boxear antes de chamar o runtime: nv_write espera Value*
        emit_write(ctx, arg, true);

        // write retorna "void" no nível da linguagem; em IR usamos um Value indefinido.
        return llvm::UndefValue::get(ValueTy);
    }
    
    // Fallback para implementação antiga

    if (name == "exit") {
        if (args.size() != 1) return nullptr;
        args[0]->codegen(ctx);
        llvm::Value* code_val = ctx.pop_value();
        llvm::Value* i32_code = value_to_i32(ctx, code_val);
        auto* I32 = llvm::Type::getInt32Ty(ctx.get_context());
        auto* VoidTy = llvm::Type::getVoidTy(ctx.get_context());
        auto* exit_fn = ctx.ensure_runtime_func("_exit", {I32}, VoidTy);
        B.CreateCall(exit_fn, {i32_code});
        B.CreateUnreachable();
        return llvm::UndefValue::get(ir_utils::get_value_struct(ctx));
    }

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
            
            // If value generation failed, treat as null/undef and emit write with empty string
            if (!val) {
                auto* empty = B.CreateGlobalStringPtr("");
                emit_write(ctx, empty);
                return llvm::UndefValue::get(ir_utils::get_value_struct(ctx));
            }
            
            // If we're generating code for a REPL loop body, box the argument,
            // store it into the out_result pointer for the host, and return the boxed value.
            auto* ValueTy = ir_utils::get_value_struct(ctx);
            if (ctx.is_repl_loop_mode() && ctx.get_repl_out_result_ptr()) {
                // Box the value (or reuse if already boxed)
                llvm::Value* boxed_alloca = nullptr;
                if (val->getType() == ValueTy) {
                    // Already boxed: store into a preserve alloca
                    boxed_alloca = ctx.create_alloca(ValueTy, "write_preserve");
                    B.CreateStore(val, boxed_alloca);
                } else {
                    boxed_alloca = box_value(ctx, val);
                }
                // Load the boxed Value and store it into out_result
                auto* boxed_loaded = B.CreateLoad(ValueTy, boxed_alloca);
                B.CreateStore(boxed_loaded, ctx.get_repl_out_result_ptr());
                // Return the boxed value (loaded)
                return boxed_loaded;
            }

            // Emit write with the value (this will box it internally)
            emit_write(ctx, val);

            // Return the original value so it can be used as program return value
            // IMPORTANT: We need to preserve the value by creating a copy in an alloca
            // This ensures the value survives optimization and is available for program return
            if (val->getType() == ValueTy) {
                // Create a copy in an alloca to preserve the value
                auto* preserve_alloca = ctx.create_alloca(ValueTy, "write_result");
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
        auto* out = ctx.create_alloca(ValueTy, "out");
        B.CreateCall(fn, {out, selfAlloca});
        return B.CreateLoad(ValueTy, out);
    } else if (method == "replace") {
        if (argv.size() < 2) return nullptr;
        auto* fn = ctx.ensure_runtime_func("string_replace", {ValuePtr, ValuePtr, ValuePtr, ValuePtr});
        auto* out = ctx.create_alloca(ValueTy, "out");
        llvm::Value* a0 = box_value(ctx, argv[0]);
        llvm::Value* a1 = box_value(ctx, argv[1]);
        B.CreateCall(fn, {out, selfAlloca, a0, a1});
        return B.CreateLoad(ValueTy, out);
    } else if (method == "includes") {
        if (argv.empty()) return nullptr;
        auto* fn = ctx.ensure_runtime_func("string_includes", {ValuePtr, ValuePtr, ValueTy});
        auto* out = ctx.create_alloca(ValueTy, "out");
        llvm::Value* v = B.CreateLoad(ValueTy, box_value(ctx, argv[0]));
        B.CreateCall(fn, {out, selfAlloca, v});
        return B.CreateLoad(ValueTy, out);
    }

    // Vector methods (explicit signatures)
    if (method == "push") {
        if (argv.empty()) return nullptr;
        auto* fn = ctx.ensure_runtime_func("vector_push_method", {ValuePtr, ValuePtr, ValuePtr});
        auto* out = ctx.create_alloca(ValueTy, "out");
        llvm::Value* valPtr = box_value(ctx, argv[0]);
        B.CreateCall(fn, {out, selfAlloca, valPtr});
        return B.CreateLoad(ValueTy, out);
    } else if (method == "pop") {
        auto* fn = ctx.ensure_runtime_func("vector_pop_method", {ValuePtr, ValuePtr});
        auto* out = ctx.create_alloca(ValueTy, "out");
        B.CreateCall(fn, {out, selfAlloca});
        return B.CreateLoad(ValueTy, out);
    } else if (method == "get") {
        if (argv.empty()) return nullptr;
        auto* fn = ctx.ensure_runtime_func("vector_get_method", {ValuePtr, ValuePtr, llvm::Type::getInt32Ty(ctx.get_context())});
        auto* out = ctx.create_alloca(ValueTy, "out");
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

        // === ESPECIAL: json.parseString("json_string") ===
        if (method == "parseString") {
            // Registrar features JSON
            nv::register_feature("json");
            nv::register_feature("string");
            
            // Verifica se o objeto é o identifier "json"
            if (auto* objId = dynamic_cast<IdentifierNode*>(mem->object.get())) {
                if (objId->symbol == "json") {
                    if (args.empty()) {
                        ctx.push_value(nullptr);
                        return;
                    }

                    // Criar variável global para o resultado
                    auto* ValueTy = ir_utils::get_value_struct(ctx);
                    auto* global_result = new llvm::GlobalVariable(
                        ctx.get_module(),
                        ValueTy,
                        false,
                        llvm::GlobalValue::InternalLinkage,
                        llvm::Constant::getNullValue(ValueTy),
                        "json_parse_string_global"
                    );

                    // Avalia o argumento (json_string)
                    args[0]->codegen(ctx);
                    llvm::Value* json_string = ctx.pop_value();
                    auto* I8P = ir_utils::get_i8_ptr(ctx);

                    if (!json_string->getType()->isPointerTy()) {
                        json_string = ctx.get_builder().CreateGlobalStringPtr(""); // fallback
                    } else if (json_string->getType() != I8P) {
                        json_string = ctx.get_builder().CreateBitCast(json_string, I8P);
                    }

                    // Chama void json_parse_string(Value*, const char*)
                    auto* ValuePtr = ir_utils::get_value_ptr(ctx);
                    auto* fn = ctx.ensure_runtime_func(
                        "json_parse_string",
                        {ValuePtr, I8P},
                        llvm::Type::getVoidTy(ctx.get_context())
                    );
                    ctx.get_builder().CreateCall(fn, {global_result, json_string});
                    // Retornar o ponteiro global
                    ctx.push_value(global_result);
                    return;
                }
            }
        }
        if (method == "parse") {
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

                    // Chama void json_parse(Value*, const char*) e carrega o Value
                    auto* ValueTy = ir_utils::get_value_struct(ctx);
                    auto* ValuePtr = ir_utils::get_value_ptr(ctx);
                    auto* fn = ctx.ensure_runtime_func(
                        "json_parse",
                        {ValuePtr, I8P},
                        llvm::Type::getVoidTy(ctx.get_context())
                    );
                    auto* outAlloca = ctx.create_alloca(ValueTy, "json_out");
                    ctx.get_builder().CreateCall(fn, {outAlloca, filename});
                    llvm::Value* loaded = ctx.get_builder().CreateLoad(ValueTy, outAlloca);
                    ctx.push_value(loaded);
                    return;
                }
            }
        }

        // === ESPECIAL: json.dump(value, "file.json") ===
        if (method == "dump") {
            // Verifica se o objeto é o identifier "json"
            if (auto* objId = dynamic_cast<IdentifierNode*>(mem->object.get())) {
                if (objId->symbol == "json") {
                    if (args.size() < 2) {
                        ctx.push_value(nullptr);
                        return;
                    }

                    // Avalia os argumentos (value, filename)
                    args[0]->codegen(ctx);
                    llvm::Value* value = ctx.pop_value();
                    
                    args[1]->codegen(ctx);
                    llvm::Value* filename = ctx.pop_value();
                    auto* I8P = ir_utils::get_i8_ptr(ctx);

                    if (!filename->getType()->isPointerTy()) {
                        filename = ctx.get_builder().CreateGlobalStringPtr(""); // fallback
                    } else if (filename->getType() != I8P) {
                        filename = ctx.get_builder().CreateBitCast(filename, I8P);
                    }

                    // Chama void json_dump(const Value*, const char*)
                    auto* ValueTy = ir_utils::get_value_struct(ctx);
                    auto* ValuePtr = ir_utils::get_value_ptr(ctx);
                    auto* fn = ctx.ensure_runtime_func(
                        "json_dump",
                        {ValuePtr, I8P},
                        llvm::Type::getVoidTy(ctx.get_context())
                    );
                    
                    // Se o valor não for um ponteiro para Value, fazer boxing
                    if (!value->getType()->isPointerTy() || value->getType() != ValuePtr) {
                        auto* valueAlloca = ctx.create_alloca(ValueTy, "json_dump_value");
                        ctx.get_builder().CreateStore(value, valueAlloca);
                        value = valueAlloca;
                    }
                    
                    ctx.get_builder().CreateCall(fn, {value, filename});
                    ctx.push_value(nullptr); // dump retorna void
                    return;
                }
            }
        }

        // === ESPECIAL: json.stringify(value) ===
        if (method == "stringify") {
            // Verifica se o objeto é o identifier "json"
            if (auto* objId = dynamic_cast<IdentifierNode*>(mem->object.get())) {
                if (objId->symbol == "json") {
                    if (args.empty()) {
                        ctx.push_value(nullptr);
                        return;
                    }

                    // Avalia o argumento (value)
                    args[0]->codegen(ctx);
                    llvm::Value* value = ctx.pop_value();

                    // Chama void json_stringify(Value* out, const Value*)
                    auto* ValueTy = ir_utils::get_value_struct(ctx);
                    auto* ValuePtr = ir_utils::get_value_ptr(ctx);
                    auto* fn = ctx.ensure_runtime_func(
                        "json_stringify",
                        {ValuePtr, ValuePtr},
                        llvm::Type::getVoidTy(ctx.get_context())
                    );
                    
                    auto* outAlloca = ctx.create_alloca(ValueTy, "json_stringify_out");
                    
                    // Se o valor não for um ponteiro para Value, fazer boxing
                    if (!value->getType()->isPointerTy() || value->getType() != ValuePtr) {
                        auto* valueAlloca = ctx.create_alloca(ValueTy, "json_stringify_value");
                        ctx.get_builder().CreateStore(value, valueAlloca);
                        value = valueAlloca;
                    }
                    
                    ctx.get_builder().CreateCall(fn, {outAlloca, value});
                    llvm::Value* result = ctx.get_builder().CreateLoad(ValueTy, outAlloca);
                    ctx.push_value(result);
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
        auto* ValuePtr = ir_utils::get_value_ptr(ctx);
        auto* I32 = llvm::Type::getInt32Ty(ctx.get_context());
        auto* I64 = llvm::Type::getInt64Ty(ctx.get_context());
        auto* F64 = llvm::Type::getDoubleTy(ctx.get_context());
        
        for (size_t i = 0; i < args.size() && i < param_types.size(); ++i) {
            args[i]->codegen(ctx);
            llvm::Value* arg_val = ctx.pop_value();
            
            // Se o argumento é Value struct mas a função espera tipo primitivo, extrair o valor
            if (arg_val && arg_val->getType() == ValueTy && param_types[i] != ValueTy) {
                // Extrair valor primitivo do Value struct
                auto* tmp_alloca = ctx.create_alloca(ValueTy, "arg_tmp");
                B.CreateStore(arg_val, tmp_alloca);
                
                // Garantir que o Value tenha o tipo correto em runtime
                auto* ensure_fn = ctx.ensure_runtime_func("ensure_value_type", {ValuePtr});
                B.CreateCall(ensure_fn, {tmp_alloca});

                // Para a nova estrutura, precisamos extrair o valor usando funções runtime
                if (param_types[i]->isIntegerTy()) {
                    // Usar função runtime para extrair int
                    auto* extract_int_func = ctx.ensure_runtime_func("extract_int_from_value", {I32, ValuePtr});
                    arg_val = B.CreateCall(extract_int_func, {tmp_alloca}, "arg_int_val");
                } else if (param_types[i]->isFloatingPointTy()) {
                    // Usar função runtime para extrair float
                    auto* extract_float_func = ctx.ensure_runtime_func("extract_float_from_value", {F64, ValuePtr});
                    arg_val = B.CreateCall(extract_float_func, {tmp_alloca}, "arg_float_val");
                }
            }
            
            argv.push_back(arg_val);
        }
        
        auto* call = B.CreateCall(F, argv);
        // Se a função retorna um tipo primitivo, converter para Value struct para manter consistência
        if (!call->getType()->isVoidTy()) {
            if (call->getType() != ValueTy) {
                // Converter retorno primitivo para Value struct
                auto* ret_alloca = ctx.create_alloca(ValueTy, "ret_val");
                auto* ValuePtr = ir_utils::get_value_ptr(ctx);
                
                // DEBUG: Verificar o tipo de retorno
                auto* debug_func = ctx.get_module().getFunction("printf");
                if (!debug_func) {
                    auto* I8Ptr_debug = llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(ctx.get_context()));
                    auto* printf_ty = llvm::FunctionType::get(llvm::Type::getInt32Ty(ctx.get_context()), {I8Ptr_debug}, true);
                    debug_func = llvm::Function::Create(printf_ty, llvm::Function::ExternalLinkage, "printf", ctx.get_module());
                }
                auto* type_name = call->getType()->isPointerTy() ? "pointer" : 
                                 call->getType()->isIntegerTy() ? "integer" :
                                 call->getType()->isFloatingPointTy() ? "float" : "unknown";
                auto* debug_msg = B.CreateGlobalStringPtr("DEBUG: Return type is %s\n");
                B.CreateCall(debug_func, {debug_msg, B.CreateGlobalStringPtr(type_name)});
                
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
                } else if (call->getType()->isPointerTy()) {
                    // Tratar como string
                    auto* create_str_func = ctx.ensure_runtime_func("create_str", {ValuePtr, call->getType()});
                    B.CreateCall(create_str_func, {ret_alloca, call});
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