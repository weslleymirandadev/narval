#include "frontend/ast/expressions/call_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"

namespace {

using namespace rph;

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
    else {
        B.CreateStore(llvm::UndefValue::get(ValueTy), alloca);
    }
    return alloca;
}

// === HELPER: Emit rph_write ===
void emit_write(IRGenerationContext& ctx, llvm::Value* v, bool newline = true) {
    // If value generation failed, emit undef Value
    if (!v) {
        v = llvm::UndefValue::get(ir_utils::get_value_struct(ctx));
    }
    auto* boxed = box_value(ctx, v);
    const char* name = newline ? "rph_write" : "rph_write_no_nl";
    auto* fn = ctx.ensure_runtime_func(name, {ir_utils::get_value_ptr(ctx)});
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
        } else {
            args[0]->codegen(ctx);
            emit_write(ctx, ctx.pop_value());
        }
        // Return an undef Value to signal handled builtin and push a Value-like result
        return llvm::UndefValue::get(ir_utils::get_value_struct(ctx));
    }

    if (name == "read") {
        if (!args.empty()) {
            args[0]->codegen(ctx);
            emit_write(ctx, ctx.pop_value(), false);
        }
        auto* fn = ctx.ensure_runtime_func("rph_read", {}, I8P);
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

    std::vector<llvm::Value*> argv;
    for (auto& a : args) {
        a->codegen(ctx);
        argv.push_back(ctx.pop_value());
    }

    if (auto* F = llvm::dyn_cast<llvm::Function>(callee)) {
        auto* call = B.CreateCall(F, argv);
        // Preserve non-void results regardless of current use count, so they can be used by callers
        ctx.push_value(call->getType()->isVoidTy() ? nullptr : call);
    } else {
        ctx.push_value(nullptr);
    }
}