#include "frontend/ast/expressions/call_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "backend/codegen/generate_ir.hpp"
#include "backend/codegen/method_handler.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/checker/type.hpp"

namespace nv {
    class BuiltinHandler;
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

// === BUILTIN: write, read, exit ===
llvm::Value* try_lower_builtin(IRGenerationContext& ctx, const std::string& name, const std::vector<std::unique_ptr<ArgNode>>& args) {
    auto& B = ctx.get_builder();
    auto* ValueTy = ir_utils::get_value_struct(ctx);
    auto* I8P = llvm::PointerType::getUnqual(ctx.get_context());

    // Implementar write diretamente
    if (name == "write") {
        if (args.empty()) {
            auto* empty = B.CreateGlobalString("");
            emit_write(ctx, empty, true);
            return llvm::UndefValue::get(ValueTy);
        }

        // Gerar código para o argumento
        if (args[0]->value) args[0]->value->codegen(ctx);
        if (!ctx.has_value()) return nullptr;

        llvm::Value* arg = ctx.pop_value();

        // Sempre boxear antes de chamar o runtime: nv_write espera Value*
        emit_write(ctx, arg, true);

        // write retorna "void" no nível da linguagem; em IR usamos um Value indefinido.
        return llvm::UndefValue::get(ValueTy);
    }
    
    // str/int/float/bool conversions
    auto try_convert = [&](const char* fn_name, const char* alloca_name) -> llvm::Value* {
        if (args.empty()) return llvm::UndefValue::get(ValueTy);
        if (args[0]->value) args[0]->value->codegen(ctx);
        if (!ctx.has_value()) return llvm::UndefValue::get(ValueTy);
        llvm::Value* arg = ctx.pop_value();
        auto* out = ctx.create_alloca(ValueTy, alloca_name);
        auto* in_box = box_value(ctx, arg);
        auto* fn = ctx.ensure_runtime_func(fn_name,
            {ir_utils::get_value_ptr(ctx), ir_utils::get_value_ptr(ctx)},
            llvm::Type::getVoidTy(ctx.get_context()));
        B.CreateCall(fn, {out, in_box});
        return B.CreateLoad(ValueTy, out);
    };
    if (name == "str")   return try_convert("nv_str_convert",   "str_out");
    if (name == "int")   return try_convert("nv_int_convert",   "int_out");
    if (name == "char")  return try_convert("nv_char_convert",  "chr_out");
    if (name == "float") return try_convert("nv_float_convert", "flt_out");
    if (name == "bool")  return try_convert("nv_bool_convert",  "boo_out");

    if (name == "exit") {
        if (args.size() != 1) return nullptr;
        if (args[0]->value) args[0]->value->codegen(ctx);
        llvm::Value* code_val = ctx.pop_value();
        llvm::Value* i32_code = value_to_i32(ctx, code_val);
        auto* I32 = llvm::Type::getInt32Ty(ctx.get_context());
        auto* VoidTy = llvm::Type::getVoidTy(ctx.get_context());
        auto* exit_fn = ctx.ensure_runtime_func("_exit", {I32}, VoidTy);
        B.CreateCall(exit_fn, {i32_code});
        B.CreateUnreachable();
        return llvm::UndefValue::get(ir_utils::get_value_struct(ctx));
    }

    if (name == "read") {
        auto* I8P = llvm::PointerType::getUnqual(ctx.get_context());
        auto* ValueTy = ir_utils::get_value_struct(ctx);
        auto* ValuePtr = ir_utils::get_value_ptr(ctx);

        llvm::Value* prompt_ptr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(I8P));
        if (!args.empty()) {
            if (args[0]->value) args[0]->value->codegen(ctx);
            if (ctx.has_value()) {
                llvm::Value* v = ctx.pop_value();
                if (v->getType() == I8P) {
                    prompt_ptr = v;
                } else if (v->getType() == ValueTy) {
                    auto* tmp = ctx.create_alloca(ValueTy, "read_prompt_val");
                    B.CreateStore(v, tmp);
                    auto* extractFn = ctx.ensure_runtime_func("extract_string_from_value", {ValuePtr}, I8P);
                    prompt_ptr = B.CreateCall(extractFn, {tmp}, "prompt_str");
                } else {
                    // Box arbitrary value and extract string representation
                    llvm::Value* boxed = box_value(ctx, v); // returns Value* (alloca)
                    auto* extractFn = ctx.ensure_runtime_func("extract_string_from_value", {ValuePtr}, I8P);
                    prompt_ptr = B.CreateCall(extractFn, {boxed}, "prompt_str");
                }
            }
        }

        auto* fn = ctx.ensure_runtime_func("nv_read", {I8P}, I8P);
        return B.CreateCall(fn, {prompt_ptr});
    }
    
    // Option / Result constructors
    auto* ValuePtr = ir_utils::get_value_ptr(ctx);
    auto box_arg = [&]() -> llvm::Value* {
        if (args.empty()) return nullptr;
        if (args[0]->value) args[0]->value->codegen(ctx);
        if (!ctx.has_value()) return nullptr;
        llvm::Value* v = ctx.pop_value();
        auto* slot = ctx.create_alloca(ValueTy, "wrap_arg");
        if (v && v->getType() == ValueTy) {
            B.CreateStore(v, slot);
        } else if (v) {
            auto* I32 = llvm::Type::getInt32Ty(ctx.get_context());
            auto* F64 = llvm::Type::getDoubleTy(ctx.get_context());
            if (v->getType()->isIntegerTy(1)) {
                B.CreateCall(ctx.ensure_runtime_func("create_bool",  {ValuePtr, I32}), {slot, B.CreateZExt(v, I32)});
            } else if (v->getType()->isIntegerTy()) {
                auto* iv = v->getType()->isIntegerTy(32) ? v : B.CreateSExtOrTrunc(v, I32);
                B.CreateCall(ctx.ensure_runtime_func("create_int",   {ValuePtr, I32}), {slot, iv});
            } else if (v->getType()->isFloatingPointTy()) {
                auto* fv = v->getType() == F64 ? v : B.CreateFPExt(v, F64);
                B.CreateCall(ctx.ensure_runtime_func("create_float", {ValuePtr, F64}), {slot, fv});
            } else if (v->getType() == I8P) {
                B.CreateCall(ctx.ensure_runtime_func("create_str",   {ValuePtr, I8P}), {slot, v});
            }
        }
        return slot; // returns Value* (pointer)
    };
    if (name == "Some") {
        auto* in  = box_arg();
        auto* out = ctx.create_alloca(ValueTy, "some.out");
        if (in) B.CreateCall(ctx.ensure_runtime_func("create_option_some", {ValuePtr, ValuePtr}), {out, in});
        return B.CreateLoad(ValueTy, out, "some.val");
    }
    if (name == "Ok") {
        auto* in  = box_arg();
        auto* out = ctx.create_alloca(ValueTy, "ok.out");
        if (in) B.CreateCall(ctx.ensure_runtime_func("create_result_ok", {ValuePtr, ValuePtr}), {out, in});
        return B.CreateLoad(ValueTy, out, "ok.val");
    }
    if (name == "Err") {
        auto* in  = box_arg();
        auto* out = ctx.create_alloca(ValueTy, "err.out");
        if (in) B.CreateCall(ctx.ensure_runtime_func("create_result_err", {ValuePtr, ValuePtr}), {out, in});
        return B.CreateLoad(ValueTy, out, "err.val");
    }

    // tensor_zeros(d0, d1, ...) / tensor_ones(d0, d1, ...)
    if (name == "tensor_zeros" || name == "tensor_ones") {
        auto* I32    = llvm::Type::getInt32Ty(ctx.get_context());
        auto* I64    = llvm::Type::getInt64Ty(ctx.get_context());
        auto* I64Ptr = llvm::PointerType::getUnqual(ctx.get_context());
        auto* ValuePtr = ir_utils::get_value_ptr(ctx);
        int32_t ndim = static_cast<int32_t>(args.size());
        // Build shape array on the stack: int64_t shape[ndim]
        auto* shape_arr = ctx.create_alloca(
            llvm::ArrayType::get(I64, ndim ? ndim : 1), "tensor_shape");
        for (int i = 0; i < ndim; ++i) {
            if (args[i]->value) args[i]->value->codegen(ctx);
            llvm::Value* dim_v = ctx.has_value() ? ctx.pop_value() : llvm::ConstantInt::get(I64, 0);
            // Coerce to i64
            if (dim_v->getType() == ValueTy) {
                auto* tmp = ctx.create_alloca(ValueTy, "dim_box");
                B.CreateStore(dim_v, tmp);
                auto* to_int = ctx.ensure_runtime_func("nv_int_convert", {ValuePtr, ValuePtr});
                auto* out = ctx.create_alloca(ValueTy, "dim_int");
                B.CreateCall(to_int, {out, tmp});
                // Extract int field from Value struct (field 0 is NvObject*)
                auto* fn_e = ctx.ensure_runtime_func("nv_value_to_i64", {ValuePtr}, I64);
                dim_v = B.CreateCall(fn_e, {out}, "dim_i64");
            } else if (dim_v->getType()->isIntegerTy()) {
                dim_v = B.CreateSExtOrTrunc(dim_v, I64, "dim_i64");
            } else {
                dim_v = llvm::ConstantInt::get(I64, 0);
            }
            auto* gep = B.CreateGEP(llvm::ArrayType::get(I64, ndim ? ndim : 1), shape_arr,
                                    {llvm::ConstantInt::get(I64, 0),
                                     llvm::ConstantInt::get(I64, i)}, "dim_ptr");
            B.CreateStore(dim_v, gep);
        }
        // shape_ptr: ptr to first element
        auto* shape_ptr = B.CreateGEP(llvm::ArrayType::get(I64, ndim ? ndim : 1), shape_arr,
                                      {llvm::ConstantInt::get(I64, 0),
                                       llvm::ConstantInt::get(I64, 0)}, "shape_ptr");
        const char* rt_fn = (name == "tensor_zeros") ? "nv_tensor_zeros" : "nv_tensor_ones";
        auto* fn = ctx.ensure_runtime_func(rt_fn, {I32, I32, I64Ptr}, ValueTy);
        // dtype=2 (NV_FLOAT_BASE), ndim, shape*
        return B.CreateCall(fn,
            {llvm::ConstantInt::get(I32, 2),
             llvm::ConstantInt::get(I32, ndim),
             shape_ptr}, "tensor_val");
    }

    return nullptr; // not builtin
}

llvm::Value* lower_method_call(IRGenerationContext& ctx, llvm::Value* selfAlloca, const std::string& method, const std::vector<llvm::Value*>& argv) {
    auto& B = ctx.get_builder();
    auto* ValueTy = ir_utils::get_value_struct(ctx);
    auto* ValuePtr = ir_utils::get_value_ptr(ctx);

    // Load self to get prototype
    llvm::Value* selfVal = B.CreateLoad(ValueTy, selfAlloca);
    (void)selfVal;

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

    // === 1. BUILTIN CALLS (write, read) ===
    if (caller->kind == NodeType::Identifier) {
        auto* id = static_cast<IdentifierNode*>(caller.get());
        std::string name = id->symbol;
        if (auto* result = try_lower_builtin(ctx, name, args)) {
            ctx.push_value(result);
            return;
        }
    }

    // === 2. METHOD CALL: obj.method(...) ===
    if (caller->kind == NodeType::MemberExpression) {
        auto* mem = static_cast<MemberExprNode*>(caller.get());

        // === PYTHON NAMESPACE: from extern "Python:mod" import * as alias → alias.method(...) ===
        if (mem->object->kind == NodeType::Identifier) {
            auto* obj_id = static_cast<IdentifierNode*>(mem->object.get());
            if (ctx.is_py_namespace(obj_id->symbol)) {
                const std::string& alias = obj_id->symbol;
                std::string method_name;
                if (mem->property->kind == NodeType::Identifier)
                    method_name = static_cast<IdentifierNode*>(mem->property.get())->symbol;

                auto* ValueTy  = ir_utils::get_value_struct(ctx);
                auto* ValuePtr = ir_utils::get_value_ptr(ctx);
                auto& C        = ctx.get_context();
                auto* I32      = llvm::Type::getInt32Ty(C);
                auto* I8P      = llvm::PointerType::getUnqual(C);
                // Value** (opaque ptr no LLVM 15+)
                auto* VPP      = llvm::PointerType::getUnqual(C);

                int n = (int)args.size();

                // Avaliar e alocar cada argumento como Value*
                std::vector<llvm::Value*> arg_slots;
                for (auto& a : args) {
                    if (a->value) a->value->codegen(ctx);
                    auto* av = ctx.pop_value();
                    auto* slot = ctx.create_alloca(ValueTy, "py_ns_arg");
                    if (av && av->getType() == ValueTy) {
                        B.CreateStore(av, slot);
                    } else if (av) {
                        auto* boxed = box_value(ctx, av);
                        llvm::Value* to_store = boxed
                            ? static_cast<llvm::Value*>(B.CreateLoad(ValueTy, boxed))
                            : static_cast<llvm::Value*>(llvm::UndefValue::get(ValueTy));
                        B.CreateStore(to_store, slot);
                    }
                    arg_slots.push_back(slot);
                }

                // Construir array Value*[] na stack
                llvm::Value* args_ptr = llvm::ConstantPointerNull::get(
                    llvm::cast<llvm::PointerType>(VPP));
                if (n > 0) {
                    auto* arr_ty = llvm::ArrayType::get(ValuePtr, n);
                    auto* arr    = ctx.create_alloca(arr_ty, "py_ns_args_arr");
                    auto* zero   = llvm::ConstantInt::get(I32, 0);
                    for (int i = 0; i < n; i++) {
                        auto* idx = llvm::ConstantInt::get(I32, i);
                        auto* gep = B.CreateGEP(arr_ty, arr, {zero, idx});
                        B.CreateStore(arg_slots[i], gep);
                    }
                    args_ptr = B.CreateGEP(arr_ty, arr,
                        {llvm::ConstantInt::get(I32, 0), llvm::ConstantInt::get(I32, 0)});
                }

                // Chamar dispatcher: Value _nv_py_ns_call_ALIAS(i8* method, Value** args, i32 n)
                std::string disp_name = "_nv_py_ns_call_" + alias;
                auto* disp_fn = ctx.get_module().getFunction(disp_name);
                if (!disp_fn) {
                    auto* fty = llvm::FunctionType::get(ValueTy, {I8P, VPP, I32}, false);
                    disp_fn = llvm::Function::Create(
                        fty, llvm::Function::ExternalLinkage, disp_name, ctx.get_module());
                }

                auto* method_str = B.CreateGlobalString(method_name, "py_ns_method");
                auto* count      = llvm::ConstantInt::get(I32, n);
                auto* result     = B.CreateCall(disp_fn, {method_str, args_ptr, count});
                ctx.push_value(result);
                return;
            }
        }

        // === NAMESPACE ALIAS: import * as ALIAS → ALIAS.func(...) ===
        if (mem->object->kind == NodeType::Identifier) {
            auto* obj_id = static_cast<IdentifierNode*>(mem->object.get());
            if (ctx.is_namespace_alias(obj_id->symbol)) {
                std::string member_name;
                if (mem->property->kind == NodeType::Identifier) {
                    member_name = static_cast<IdentifierNode*>(mem->property.get())->symbol;
                }
                if (!member_name.empty()) {
                    auto* fn = ctx.get_module().getFunction(member_name);
                    if (fn) {
                        auto* ValueTy  = ir_utils::get_value_struct(ctx);
                        auto* ValuePtr = ir_utils::get_value_ptr(ctx);
                        auto* I32 = llvm::Type::getInt32Ty(ctx.get_context());
                        auto* I64 = llvm::Type::getInt64Ty(ctx.get_context());
                        auto* F64 = llvm::Type::getDoubleTy(ctx.get_context());

                        // Usar os tipos reais dos parâmetros da função (mesma lógica do caminho normal)
                        auto* func_ty = fn->getFunctionType();
                        std::vector<llvm::Type*> param_types(func_ty->param_begin(), func_ty->param_end());

                        std::vector<llvm::Value*> call_args;
                        for (size_t i = 0; i < args.size(); ++i) {
                            if (args[i]->value) args[i]->value->codegen(ctx);
                            auto* av = ctx.pop_value();

                            if (i < param_types.size()) {
                                auto* expected = param_types[i];
                                // Value struct passado mas função espera primitivo: extrair
                                if (av && av->getType() == ValueTy && expected != ValueTy) {
                                    auto* tmp = ctx.create_alloca(ValueTy, "ns_arg_tmp");
                                    B.CreateStore(av, tmp);
                                    auto* ensure_fn = ctx.ensure_runtime_func("ensure_value_type", {ValuePtr});
                                    B.CreateCall(ensure_fn, {tmp});
                                    if (expected->isIntegerTy()) {
                                        auto* f = ctx.ensure_runtime_func("extract_int_from_value", {I32, ValuePtr});
                                        av = B.CreateCall(f, {tmp}, "ns_iarg");
                                        if (!expected->isIntegerTy(32))
                                            av = B.CreateSExtOrTrunc(av, expected);
                                    } else if (expected->isFloatingPointTy()) {
                                        auto* f = ctx.ensure_runtime_func("extract_float_from_value", {F64, ValuePtr});
                                        av = B.CreateCall(f, {tmp}, "ns_farg");
                                    } else if (expected->isPointerTy() && av->getType() == ValueTy) {
                                        // pointer expected: box into alloca and pass ptr
                                        auto* slot = ctx.create_alloca(ValueTy, "ns_arg");
                                        B.CreateStore(av, slot);
                                        av = slot;
                                    }
                                } else if (av && av->getType() != ValueTy && expected == ValueTy) {
                                    // Primitivo mas função espera Value struct: box
                                    auto* slot = ctx.create_alloca(ValueTy, "ns_arg");
                                    if (av->getType()->isIntegerTy(1)) {
                                        auto* f = ctx.ensure_runtime_func("create_bool", {ValuePtr, I32});
                                        B.CreateCall(f, {slot, B.CreateZExt(av, I32)});
                                    } else if (av->getType()->isIntegerTy()) {
                                        auto* iv = av->getType()->isIntegerTy(32) ? av : B.CreateSExtOrTrunc(av, I32);
                                        auto* f = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
                                        B.CreateCall(f, {slot, iv});
                                    } else if (av->getType()->isFloatingPointTy()) {
                                        auto* fv = av->getType() == F64 ? av : B.CreateFPExt(av, F64);
                                        auto* f = ctx.ensure_runtime_func("create_float", {ValuePtr, F64});
                                        B.CreateCall(f, {slot, fv});
                                    } else {
                                        B.CreateStore(llvm::UndefValue::get(ValueTy), slot);
                                    }
                                    av = B.CreateLoad(ValueTy, slot);
                                } else if (av && av->getType()->isPointerTy() && expected == ValueTy) {
                                    // Ponteiro para Value, mas espera Value by-value: load
                                    av = B.CreateLoad(ValueTy, av);
                                }
                            }
                            if (av && av->getType() != param_types[i] &&
                                ((av->getType()->isIntegerTy() && param_types[i]->isFloatingPointTy()) ||
                                 (av->getType()->isFloatingPointTy() && param_types[i]->isIntegerTy()) ||
                                 (av->getType()->isIntegerTy() && param_types[i]->isIntegerTy()))) {
                                av = ir_utils::promote_type(ctx, av, param_types[i]);
                            }
                            call_args.push_back(av);
                        }

                        auto* result = B.CreateCall(fn, call_args);
                        if (!fn->getReturnType()->isVoidTy()) {
                            // Converter retorno primitivo para Value se necessário
                            if (result->getType() != ValueTy) {
                                auto* ret_alloca = ctx.create_alloca(ValueTy, "ns_ret");
                                if (result->getType() == I32) {
                                    auto* f = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
                                    B.CreateCall(f, {ret_alloca, result});
                                } else if (result->getType() == I64) {
                                    auto* i32v = B.CreateTrunc(result, I32);
                                    auto* f = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
                                    B.CreateCall(f, {ret_alloca, i32v});
                                } else if (result->getType() == F64) {
                                    auto* f = ctx.ensure_runtime_func("create_float", {ValuePtr, F64});
                                    B.CreateCall(f, {ret_alloca, result});
                                } else {
                                    B.CreateStore(llvm::UndefValue::get(ValueTy), ret_alloca);
                                }
                                ctx.push_value(B.CreateLoad(ValueTy, ret_alloca, "ns_ret_val"));
                            } else {
                                ctx.push_value(result);
                            }
                        } else {
                            ctx.push_value(llvm::UndefValue::get(ValueTy));
                        }
                        return;
                    }
                    // Fallback: variável global
                    auto sym = ctx.get_symbol_table().lookup_symbol(member_name);
                    if (sym.has_value() && sym->value) {
                        ctx.push_value(sym->value);
                        return;
                    }
                }
            }
        }

        // Avalia o objeto
        mem->object->codegen(ctx);
        llvm::Value* obj = ctx.pop_value();

        // Extrai o nome do método
        std::string method;
        if (mem->property->kind == NodeType::Identifier) {
            method = static_cast<IdentifierNode*>(mem->property.get())->symbol;
        } else {
            ctx.push_value(nullptr);
            return;
        }

        // === MÉTODOS DE CLASSE DEFINIDOS PELO USUÁRIO ===
        if (ctx.get_type_checker()) {
            auto* checker = static_cast<nv::Checker*>(ctx.get_type_checker());
            std::shared_ptr<nv::Type> obj_type = nullptr;
            if (mem->object->kind == NodeType::Identifier) {
                auto* obj_id = static_cast<IdentifierNode*>(mem->object.get());
                auto obj_sym = ctx.get_symbol_table().lookup_symbol(obj_id->symbol);
                if (obj_sym.has_value()) {
                    obj_type = obj_sym->nv_type;
                }
            }
            if (!obj_type) {
                obj_type = checker->infer_expr(mem->object.get());
            }
            if (obj_type) {
                obj_type = checker->unify_ctx.resolve(obj_type);
                if (obj_type && obj_type->kind == nv::Kind::CLASS) {
                    auto* cls = static_cast<nv::Class*>(obj_type.get());
                    // Subir na cadeia de herança para achar a classe que definiu o método
                    auto* defining = cls;
                    while (defining && defining->methods.find(method) == defining->methods.end()) {
                        defining = defining->parent_class.get();
                    }
                    // Para classes genéricas "Box<int>", usar nome base "Box" no lookup LLVM
                    auto strip_generics = [](const std::string& n) {
                        auto lt = n.find('<');
                        return lt != std::string::npos ? n.substr(0, lt) : n;
                    };
                    std::string fn_name = "__method_" + strip_generics(defining ? defining->name : cls->name) + "_" + method;
                    auto* method_fn = ctx.get_module().getFunction(fn_name);
                    if (method_fn) {
                        auto* ValueTy  = ir_utils::get_value_struct(ctx);
                        auto* ValuePtr = ir_utils::get_value_ptr(ctx);

                        // Build self as Value* (method ABI requires Value* as first arg).
                        auto* self_alloca = ctx.create_alloca(ValueTy, "cls_self");
                        if (obj && obj->getType() == ValueTy) {
                            B.CreateStore(obj, self_alloca);
                        } else if (obj && obj->getType()->isPointerTy()) {
                            auto* loaded = B.CreateLoad(ValueTy, obj);
                            B.CreateStore(loaded, self_alloca);
                        } else {
                            B.CreateStore(llvm::UndefValue::get(ValueTy), self_alloca);
                        }

                        // Box each argument as Value*
                        std::vector<llvm::Value*> call_args = {self_alloca};
                        for (auto& a : args) {
                            if (a->value) a->value->codegen(ctx);
                            auto* av = ctx.pop_value();
                            call_args.push_back(box_value(ctx, av));
                        }

                        auto* call = B.CreateCall(method_fn, call_args);
                        if (!method_fn->getReturnType()->isVoidTy()) {
                            ctx.push_value(call);
                        } else {
                            ctx.push_value(llvm::UndefValue::get(ir_utils::get_value_struct(ctx)));
                        }
                        return;
                    }
                }
            }
        }

        // === MÉTODOS NORMAIS de primitivos (push, pop, toUpperCase, etc) ===
        auto* selfAlloca = box_value(ctx, obj);

        std::vector<llvm::Value*> argv;
        for (auto& a : args) {
            if (a->value) a->value->codegen(ctx);
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
        
        // Detect keyword args
        bool has_keywords = false;
        for (const auto& a : args) if (!a->name.empty()) { has_keywords = true; break; }

        if (!has_keywords) {
            // First, evaluate all provided arguments
            std::vector<llvm::Value*> provided_args;
            for (size_t i = 0; i < args.size() && i < param_types.size(); ++i) {
                if (args[i]->value) args[i]->value->codegen(ctx);
                provided_args.push_back(ctx.pop_value());
            }
            
            // Fill missing arguments with default values if available
            if (ctx.get_type_checker()) {
                auto* checker = static_cast<nv::Checker*>(ctx.get_type_checker());
                if (caller->kind == NodeType::Identifier) {
                    auto* id = static_cast<IdentifierNode*>(caller.get());
                    auto defaults_it = checker->function_default_values.find(id->symbol);
                    if (defaults_it != checker->function_default_values.end()) {
                        const auto& default_values = defaults_it->second;
                        
                        // Ensure we have arguments for all parameters
                        while (provided_args.size() < param_types.size()) {
                            size_t param_idx = provided_args.size();
                            if (param_idx < default_values.size() && default_values[param_idx]) {
                                // Generate code for default value
                                default_values[param_idx]->codegen(ctx);
                                provided_args.push_back(ctx.pop_value());
                            } else {
                                // No default value available, this should have been caught by type checker
                                provided_args.push_back(nullptr);
                            }
                        }
                    }
                }
            }
            
            // Now process all arguments
            for (size_t i = 0; i < provided_args.size() && i < param_types.size(); ++i) {
                llvm::Value* arg_val = provided_args[i];

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
                // Primitivo/ponteiro → Value struct (ex: string literal passado a wrapper extern)
                else if (arg_val && arg_val->getType() != ValueTy && param_types[i] == ValueTy) {
                    auto* boxed = box_value(ctx, arg_val);
                    arg_val = B.CreateLoad(ValueTy, boxed, "arg_boxed");
                }

                if (arg_val && arg_val->getType() != param_types[i] &&
                    ((arg_val->getType()->isIntegerTy() && param_types[i]->isFloatingPointTy()) ||
                     (arg_val->getType()->isFloatingPointTy() && param_types[i]->isIntegerTy()) ||
                     (arg_val->getType()->isIntegerTy() && param_types[i]->isIntegerTy()))) {
                    arg_val = ir_utils::promote_type(ctx, arg_val, param_types[i]);
                }

                argv.push_back(arg_val);
            }
        } else {
            // Keyword args: require direct identifier caller and parameter name map from Checker
            if (caller->kind != NodeType::Identifier) {
                ctx.push_value(nullptr);
                return;
            }
            if (!ctx.get_type_checker()) {
                ctx.push_value(nullptr);
                return;
            }
            auto* checker = static_cast<nv::Checker*>(ctx.get_type_checker());
            auto* id = static_cast<IdentifierNode*>(caller.get());
            auto it = checker->function_param_names.find(id->symbol);
            if (it == checker->function_param_names.end()) {
                ctx.push_value(nullptr);
                return;
            }
            const auto& pname_list = it->second;
            if (pname_list.size() != param_types.size()) {
                ctx.push_value(nullptr);
                return;
            }

            // Evaluate all args left-to-right and collect their values
            std::vector<llvm::Value*> eval_vals;
            eval_vals.reserve(args.size());
            for (auto& a : args) {
                if (a->value) a->value->codegen(ctx);
                eval_vals.push_back(ctx.pop_value());
            }

            // Map to parameter order
            std::vector<llvm::Value*> ordered_args(param_types.size(), nullptr);
            size_t pos_index = 0;
            for (size_t ai = 0; ai < args.size(); ++ai) {
                if (args[ai]->name.empty()) {
                    if (pos_index >= ordered_args.size()) { ctx.push_value(nullptr); return; }
                    ordered_args[pos_index++] = eval_vals[ai];
                } else {
                    auto found = std::find(pname_list.begin(), pname_list.end(), args[ai]->name);
                    if (found == pname_list.end()) { ctx.push_value(nullptr); return; }
                    size_t idx = std::distance(pname_list.begin(), found);
                    if (ordered_args[idx]) { ctx.push_value(nullptr); return; }
                    ordered_args[idx] = eval_vals[ai];
                }
            }

            // Fill missing arguments with default values
            auto defaults_it = checker->function_default_values.find(id->symbol);
            if (defaults_it != checker->function_default_values.end()) {
                const auto& default_values = defaults_it->second;
                for (size_t i = 0; i < ordered_args.size(); ++i) {
                    if (!ordered_args[i] && i < default_values.size() && default_values[i]) {
                        // Generate code for default value
                        default_values[i]->codegen(ctx);
                        ordered_args[i] = ctx.pop_value();
                    }
                }
            }

            // Ensure all assigned (after filling defaults)
            for (size_t i = 0; i < ordered_args.size(); ++i) {
                if (!ordered_args[i]) { ctx.push_value(nullptr); return; }
            }

            // Now adapt each ordered arg to expected param type
            for (size_t i = 0; i < ordered_args.size() && i < param_types.size(); ++i) {
                llvm::Value* arg_val = ordered_args[i];
                if (arg_val && arg_val->getType() == ValueTy && param_types[i] != ValueTy) {
                    auto* tmp_alloca = ctx.create_alloca(ValueTy, "arg_tmp_kw");
                    B.CreateStore(arg_val, tmp_alloca);
                    auto* ensure_fn = ctx.ensure_runtime_func("ensure_value_type", {ValuePtr});
                    B.CreateCall(ensure_fn, {tmp_alloca});
                    if (param_types[i]->isIntegerTy()) {
                        auto* extract_int_func = ctx.ensure_runtime_func("extract_int_from_value", {I32, ValuePtr});
                        arg_val = B.CreateCall(extract_int_func, {tmp_alloca}, "arg_int_val");
                    } else if (param_types[i]->isFloatingPointTy()) {
                        auto* extract_float_func = ctx.ensure_runtime_func("extract_float_from_value", {F64, ValuePtr});
                        arg_val = B.CreateCall(extract_float_func, {tmp_alloca}, "arg_float_val");
                    }
                } else if (arg_val && arg_val->getType() != ValueTy && param_types[i] == ValueTy) {
                    // box primitive into Value
                    auto* slot = ctx.create_alloca(ValueTy, "kw_ns_arg");
                    if (arg_val->getType()->isIntegerTy(1)) {
                        auto* f = ctx.ensure_runtime_func("create_bool", {ValuePtr, I32});
                        B.CreateCall(f, {slot, B.CreateZExt(arg_val, I32)});
                    } else if (arg_val->getType()->isIntegerTy()) {
                        auto* iv = arg_val->getType()->isIntegerTy(32) ? arg_val : B.CreateSExtOrTrunc(arg_val, I32);
                        auto* f = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
                        B.CreateCall(f, {slot, iv});
                    } else if (arg_val->getType()->isFloatingPointTy()) {
                        auto* fv = arg_val->getType() == F64 ? arg_val : B.CreateFPExt(arg_val, F64);
                        auto* f = ctx.ensure_runtime_func("create_float", {ValuePtr, F64});
                        B.CreateCall(f, {slot, fv});
                    } else if (arg_val->getType()->isPointerTy()) {
                        auto* f = ctx.ensure_runtime_func("create_str", {ValuePtr, arg_val->getType()});
                        B.CreateCall(f, {slot, arg_val});
                    } else {
                        B.CreateStore(llvm::UndefValue::get(ValueTy), slot);
                    }
                    arg_val = B.CreateLoad(ValueTy, slot);
                } else if (arg_val && arg_val->getType()->isPointerTy() && param_types[i] == ValueTy) {
                    arg_val = B.CreateLoad(ValueTy, arg_val);
                }
                if (arg_val && arg_val->getType() != param_types[i] &&
                    ((arg_val->getType()->isIntegerTy() && param_types[i]->isFloatingPointTy()) ||
                     (arg_val->getType()->isFloatingPointTy() && param_types[i]->isIntegerTy()) ||
                     (arg_val->getType()->isIntegerTy() && param_types[i]->isIntegerTy()))) {
                    arg_val = ir_utils::promote_type(ctx, arg_val, param_types[i]);
                }
                argv.push_back(arg_val);
            }
        }
        
        auto* call = B.CreateCall(F, argv);
        // Se a função retorna um tipo primitivo, converter para Value struct para manter consistência
        // EXCETO quando estamos em um contexto de return - nesse caso, manter o tipo original
        if (!call->getType()->isVoidTy()) {
            if (call->getType() != ValueTy) {
                // Verificar se estamos em um contexto de return (dentro de uma função que retorna o mesmo tipo)
                auto* current_func = ctx.get_current_function();
                bool in_return_context = false;
                
                if (current_func && current_func->getReturnType() == call->getType()) {
                    // Estamos em um contexto de return, não converter para Value
                    in_return_context = true;
                }
                
                if (!in_return_context) {
                    // Converter retorno primitivo para Value struct
                    auto* ret_alloca = ctx.create_alloca(ValueTy, "ret_val");
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
                    } else if (call->getType()->isPointerTy()) {
                        // Tratar como string
                        auto* create_str_func = ctx.ensure_runtime_func("create_str", {ValuePtr, call->getType()});
                        B.CreateCall(create_str_func, {ret_alloca, call});
                    } else {
                        // Tipo não suportado, usar UndefValue
                        B.CreateStore(llvm::UndefValue::get(ValueTy), ret_alloca);
                    }
                    
                    // CORREÇÃO: Fazer load do ret_alloca e retornar o valor
                    auto* ret_value = B.CreateLoad(ValueTy, ret_alloca, "ret_value");
                    ctx.push_value(ret_value);
                } else {
                    // Estamos em um contexto de return, manter o tipo original
                    ctx.push_value(call);
                }
            } else {
                // Já é Value struct, apenas empurrar
                ctx.push_value(call);
            }
        } else {
            // Void return, push nullptr
            ctx.push_value(nullptr);
        }
    } else if (callee && callee->getType() == ir_utils::get_value_struct(ctx)) {
        // Callee is a runtime Value — dispatch as closure call via call_closure().
        auto* ValueTy  = ir_utils::get_value_struct(ctx);
        auto* ValuePtr = ir_utils::get_value_ptr(ctx);
        auto* I32      = llvm::Type::getInt32Ty(ctx.get_context());
        auto* VoidTy   = llvm::Type::getVoidTy(ctx.get_context());

        // Store closure Value into an alloca so we can pass Value* to call_closure
        auto* closure_alloca = ctx.create_alloca(ValueTy, "closure.obj");
        B.CreateStore(callee, closure_alloca);

        // Evaluate each argument and build a contiguous Value[] array on the stack
        int n = static_cast<int>(args.size());
        llvm::Value* args_ptr = llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ValuePtr));

        if (n > 0) {
            auto* arr_ty = llvm::ArrayType::get(ValueTy, n);
            auto* arr    = ctx.create_alloca(arr_ty, "closure.args");
            auto* zero   = llvm::ConstantInt::get(I32, 0);

            for (int i = 0; i < n; i++) {
                if (args[i]->value) args[i]->value->codegen(ctx);
                auto* av = ctx.pop_value();

                auto* slot_ptr = B.CreateGEP(arr_ty, arr,
                    {zero, llvm::ConstantInt::get(I32, i)}, "carg.slot");

                if (av && av->getType() == ValueTy) {
                    B.CreateStore(av, slot_ptr);
                } else if (av) {
                    // Box primitive into a temporary Value then store
                    auto* tmp = ctx.create_alloca(ValueTy, "carg.box");
                    auto* f64 = llvm::Type::getDoubleTy(ctx.get_context());
                    auto* i8p = ir_utils::get_i8_ptr(ctx);
                    if (av->getType()->isIntegerTy(1)) {
                        B.CreateCall(ctx.ensure_runtime_func("create_bool", {ValuePtr, I32}),
                                     {tmp, B.CreateZExt(av, I32)});
                    } else if (av->getType()->isIntegerTy()) {
                        auto* iv = av->getType()->isIntegerTy(32)
                                       ? av
                                       : B.CreateSExtOrTrunc(av, I32);
                        B.CreateCall(ctx.ensure_runtime_func("create_int", {ValuePtr, I32}),
                                     {tmp, iv});
                    } else if (av->getType()->isFloatingPointTy()) {
                        auto* fv = av->getType() == f64 ? av : B.CreateFPExt(av, f64);
                        B.CreateCall(ctx.ensure_runtime_func("create_float", {ValuePtr, f64}),
                                     {tmp, fv});
                    } else if (av->getType()->isPointerTy()) {
                        B.CreateCall(ctx.ensure_runtime_func("create_str", {ValuePtr, av->getType()}),
                                     {tmp, av});
                    } else {
                        B.CreateStore(llvm::UndefValue::get(ValueTy), tmp);
                    }
                    B.CreateStore(B.CreateLoad(ValueTy, tmp), slot_ptr);
                } else {
                    B.CreateStore(llvm::UndefValue::get(ValueTy), slot_ptr);
                }
            }

            // First element pointer = &arr[0]
            args_ptr = B.CreateGEP(arr_ty, arr,
                {zero, llvm::ConstantInt::get(I32, 0)}, "closure.args.ptr");
        }

        // Result alloca
        auto* result_alloca = ctx.create_alloca(ValueTy, "closure.result");

        // call_closure(Value* closure, Value* args, i32 argc, Value* result) : void
        auto* call_closure_fn = ctx.ensure_runtime_func(
            "call_closure", {ValuePtr, ValuePtr, I32, ValuePtr}, VoidTy);
        B.CreateCall(call_closure_fn,
                     {closure_alloca, args_ptr,
                      llvm::ConstantInt::get(I32, n), result_alloca});

        ctx.push_value(B.CreateLoad(ValueTy, result_alloca, "closure.ret"));
    } else {
        ctx.push_value(nullptr);
    }
}
