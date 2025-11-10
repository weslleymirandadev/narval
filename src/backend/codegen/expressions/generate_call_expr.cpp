#include "frontend/ast/expressions/call_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"

namespace {
    
static llvm::Function* ensure_create_str(rph::IRGenerationContext& ctx) {
    auto& M = ctx.get_module();
    auto& C = ctx.get_context();
    auto* ValueTy = rph::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = rph::ir_utils::get_value_ptr(ctx);
    auto* I8P = rph::ir_utils::get_i8_ptr(ctx);
    auto decl = M.getOrInsertFunction(
        "create_str",
        llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, I8P}, false)
    );
    return llvm::cast<llvm::Function>(decl.getCallee());
}

static llvm::Function* ensure_create_bool(rph::IRGenerationContext& ctx) {
    auto& M = ctx.get_module();
    auto& C = ctx.get_context();
    auto* ValueTy = rph::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = rph::ir_utils::get_value_ptr(ctx);
    (void)ValuePtr;
    auto* I32 = llvm::Type::getInt32Ty(C);
    auto decl = M.getOrInsertFunction(
        "create_bool",
        llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, I32}, false)
    );
    if (auto* F = llvm::dyn_cast<llvm::Function>(decl.getCallee())) {
        F->addParamAttr(0, llvm::Attribute::getWithStructRetType(C, ValueTy));
        F->addParamAttr(0, llvm::Attribute::get(C, llvm::Attribute::Alignment, 8));
    }
    return llvm::cast<llvm::Function>(decl.getCallee());
}

static llvm::Function* ensure_create_int(rph::IRGenerationContext& ctx) {
    auto& M = ctx.get_module();
    auto& C = ctx.get_context();
    auto* ValueTy = rph::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = rph::ir_utils::get_value_ptr(ctx);
    (void)ValuePtr;
    auto* I32 = llvm::Type::getInt32Ty(C);
    auto decl = M.getOrInsertFunction(
        "create_int",
        llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, I32}, false)
    );
    if (auto* F = llvm::dyn_cast<llvm::Function>(decl.getCallee())) {
        F->addParamAttr(0, llvm::Attribute::getWithStructRetType(C, ValueTy));
        F->addParamAttr(0, llvm::Attribute::get(C, llvm::Attribute::Alignment, 8));
    }
    return llvm::cast<llvm::Function>(decl.getCallee());
}

static llvm::Function* ensure_create_float(rph::IRGenerationContext& ctx) {
    auto& M = ctx.get_module();
    auto& C = ctx.get_context();
    auto* ValueTy = rph::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = rph::ir_utils::get_value_ptr(ctx);
    (void)ValuePtr;
    auto* F64 = llvm::Type::getDoubleTy(C);
    auto decl = M.getOrInsertFunction(
        "create_float",
        llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, F64}, false)
    );
    if (auto* F = llvm::dyn_cast<llvm::Function>(decl.getCallee())) {
        F->addParamAttr(0, llvm::Attribute::getWithStructRetType(C, ValueTy));
        F->addParamAttr(0, llvm::Attribute::get(C, llvm::Attribute::Alignment, 8));
    }
    return llvm::cast<llvm::Function>(decl.getCallee());
}

static void lower_write_call(rph::IRGenerationContext& ctx, std::vector<llvm::Value*>& argv) {
    auto& b = ctx.get_builder();
    auto& M = ctx.get_module();
    auto& C = ctx.get_context();
    auto* ValueTy = rph::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = rph::ir_utils::get_value_ptr(ctx);
    auto write_decl = M.getOrInsertFunction("rph_write", llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr}, false));

    std::vector<llvm::Value*> newArgs;
    if (argv.empty()) {
        auto* empty = b.CreateGlobalStringPtr("");
        auto* tmp = b.CreateAlloca(ValueTy, nullptr, "tmpv");
        auto* fn = ensure_create_str(ctx);
        b.CreateCall(fn, {tmp, empty});
        newArgs.push_back(tmp);
    } else {
        llvm::Value* a0 = argv[0];
        auto* i8 = llvm::Type::getInt8Ty(C);
        auto* i8p = llvm::PointerType::getUnqual(i8);
        auto* I32 = llvm::Type::getInt32Ty(C);
        auto* F64 = llvm::Type::getDoubleTy(C);
        if (a0->getType() == ValueTy) {
            auto* tmp = b.CreateAlloca(ValueTy, nullptr, "tmpv");
            b.CreateStore(a0, tmp);
            newArgs.push_back(tmp);
        } else if (a0->getType() == i8p) {
            auto* tmp = b.CreateAlloca(ValueTy, nullptr, "tmpv");
            auto* fn = ensure_create_str(ctx);
            b.CreateCall(fn, {tmp, a0});
            newArgs.push_back(tmp);
        } else if (a0->getType()->isIntegerTy(1)) {
            auto* tmp = b.CreateAlloca(ValueTy, nullptr, "tmpv");
            auto* fn = ensure_create_bool(ctx);
            b.CreateCall(fn, {tmp, b.CreateZExt(a0, I32)});
            newArgs.push_back(tmp);
        } else if (a0->getType()->isIntegerTy()) {
            auto* tmp = b.CreateAlloca(ValueTy, nullptr, "tmpv");
            auto* fn = ensure_create_int(ctx);
            llvm::Value* iv = a0->getType()->isIntegerTy(32) ? a0 : b.CreateSExtOrTrunc(a0, I32);
            b.CreateCall(fn, {tmp, iv});
            newArgs.push_back(tmp);
        } else if (a0->getType()->isFloatingPointTy()) {
            llvm::Value* fp = a0;
            if (a0->getType() != F64) fp = b.CreateFPExt(a0, F64);
            auto* tmp = b.CreateAlloca(ValueTy, nullptr, "tmpv");
            auto* fn = ensure_create_float(ctx);
            b.CreateCall(fn, {tmp, fp});
            newArgs.push_back(tmp);
        } else {
            auto* tmp = b.CreateAlloca(ValueTy, nullptr, "tmpv");
            b.CreateStore(llvm::UndefValue::get(ValueTy), tmp);
            newArgs.push_back(tmp);
        }
    }
    b.CreateCall(llvm::cast<llvm::Function>(write_decl.getCallee()), newArgs);
}
static llvm::Function* ensure_rph_write_no_nl(rph::IRGenerationContext& ctx) {
    auto& M = ctx.get_module();
    auto& C = ctx.get_context();
    auto* ValuePtr = rph::ir_utils::get_value_ptr(ctx);
    auto decl = M.getOrInsertFunction(
        "rph_write_no_nl",
        llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr}, false)
    );
    return llvm::cast<llvm::Function>(decl.getCallee());
}

static llvm::Function* ensure_rph_read(rph::IRGenerationContext& ctx) {
    auto& M = ctx.get_module();
    auto& C = ctx.get_context();
    auto* I8 = llvm::Type::getInt8Ty(C);
    auto* I8P = llvm::PointerType::getUnqual(I8);
    auto decl = M.getOrInsertFunction(
        "rph_read",
        llvm::FunctionType::get(I8P, {}, false)
    );
    return llvm::cast<llvm::Function>(decl.getCallee());
}

static llvm::Function* ensure_rph_write(rph::IRGenerationContext& ctx) {
    auto& M = ctx.get_module();
    auto& C = ctx.get_context();
    auto* ValueTy = rph::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = rph::ir_utils::get_value_ptr(ctx);
    auto decl = M.getOrInsertFunction(
        "rph_write",
        llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr}, false)
    );
    (void)ValueTy; // type ensured by ir_utils
    return llvm::cast<llvm::Function>(decl.getCallee());
}

static llvm::Value* box_to_value(rph::IRGenerationContext& ctx, llvm::Value* any) {
    auto& B = ctx.get_builder();
    auto& C = ctx.get_context();
    auto* ValueTy = rph::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = rph::ir_utils::get_value_ptr(ctx);
    (void)ValuePtr;
    auto* I8P = rph::ir_utils::get_i8_ptr(ctx);
    auto* box = B.CreateAlloca(ValueTy, nullptr, "tmpv");
    if (any->getType() == ValueTy) {
        B.CreateStore(any, box);
        return box;
    }
    if (any->getType() == I8P) {
        auto* f = ensure_create_str(ctx);
        B.CreateCall(f, {box, any});
        return box;
    }
    if (any->getType()->isIntegerTy(1)) {
        auto* I32 = llvm::Type::getInt32Ty(C);
        auto* f = ensure_create_bool(ctx);
        B.CreateCall(f, {box, B.CreateZExt(any, I32)});
        return box;
    }
    if (any->getType()->isIntegerTy()) {
        auto* I32 = llvm::Type::getInt32Ty(C);
        llvm::Value* iv = any->getType()->isIntegerTy(32) ? any : B.CreateSExtOrTrunc(any, I32);
        auto* f = ensure_create_int(ctx);
        B.CreateCall(f, {box, iv});
        return box;
    }
    if (any->getType()->isFloatingPointTy()) {
        auto* F64 = llvm::Type::getDoubleTy(C);
        llvm::Value* fp = any->getType() == F64 ? any : B.CreateFPExt(any, F64);
        auto* f = ensure_create_float(ctx);
        B.CreateCall(f, {box, fp});
        return box;
    }
    // fallback: undef
    B.CreateStore(llvm::UndefValue::get(ValueTy), box);
    return box;
}

static void emit_rph_write(rph::IRGenerationContext& ctx, llvm::Value* any) {
    auto& B = ctx.get_builder();
    auto* outBox = box_to_value(ctx, any);
    auto* writeFn = ensure_rph_write(ctx);
    B.CreateCall(writeFn, {outBox});
}

static void emit_rph_write_no_nl(rph::IRGenerationContext& ctx, llvm::Value* any) {
    auto& B = ctx.get_builder();
    auto* outBox = box_to_value(ctx, any);
    auto* writeFn = ensure_rph_write_no_nl(ctx);
    B.CreateCall(writeFn, {outBox});
}
}

void CallExprNode::codegen(rph::IRGenerationContext& ctx) {
    llvm::Function* callee_fn = nullptr;
    llvm::FunctionType* callee_ty = nullptr;
    llvm::Value* callee_val = nullptr;

    std::string identName;
    if (auto* id = dynamic_cast<IdentifierNode*>(caller.get())) {
        identName = id->symbol;
        callee_fn = ctx.get_module().getFunction(identName);
        if (callee_fn) callee_ty = callee_fn->getFunctionType();
    } else if (!dynamic_cast<MemberExprNode*>(caller.get())) {
        // Avalia o caller como expressão e tenta usar como função direta ou ponteiro para função
        caller->codegen(ctx);
        callee_val = ctx.pop_value();
        if (auto* F = llvm::dyn_cast_or_null<llvm::Function>(callee_val)) {
            callee_fn = F;
            callee_ty = F->getFunctionType();
            callee_val = nullptr; // usaremos CreateCall(Function*)
        }
    }

    // Handle method calls like obj.method(...)
    if (auto* mem = dynamic_cast<MemberExprNode*>(caller.get())) {
        // Evaluate object
        if (mem->object) mem->object->codegen(ctx);
        llvm::Value* objV = ctx.pop_value();

        auto& M = ctx.get_module();
        auto& C = ctx.get_context();
        auto& B = ctx.get_builder();

        auto* ValueTy = rph::ir_utils::get_value_struct(ctx);
        auto* ValuePtr = rph::ir_utils::get_value_ptr(ctx);
        auto* I8P = rph::ir_utils::get_i8_ptr(ctx);

        // Ensure object is a Value aggregate
        llvm::Value* selfAlloca = nullptr;
        if (objV->getType() == ValueTy || objV->getType() == I8P || objV->getType()->isIntegerTy() || objV->getType()->isFloatingPointTy()) {
            selfAlloca = box_to_value(ctx, objV);
        } else {
            // Unsupported receiver type
            ctx.push_value(nullptr);
            return;
        }

        // Determine method name
        std::string methodName;
        if (auto* id = dynamic_cast<IdentifierNode*>(mem->property.get())) {
            methodName = id->symbol;
        }

        // Define function pointer types for vtables
        auto* VoidTy = llvm::Type::getVoidTy(C);
        auto* Method1FTy = llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr}, false);
        auto* Method2FTy = llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, ValueTy}, false);
        auto* Method3FTy = llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, ValueTy, ValueTy}, false);
        auto* Method1Ty = Method1FTy->getPointerTo();
        auto* Method2Ty = Method2FTy->getPointerTo();
        auto* Method3Ty = Method3FTy->getPointerTo();

        // String vtable layout (kept for arrays below); we will call string methods directly by name
        auto* StringVTableTy = llvm::StructType::create(C, {Method1Ty, Method3Ty, Method2Ty}, "rph.rt.StringVTable");
        auto* ArrayVTableTy  = llvm::StructType::create(C, {Method2Ty, Method1Ty}, "rph.rt.ArrayVTable");

        // Load prototype pointer from self (used for arrays); strings will be direct-called
        llvm::Value* selfVal = B.CreateLoad(ValueTy, selfAlloca);
        llvm::Value* protoPtr = B.CreateExtractValue(selfVal, {2}, "proto"); // third field

        // box_to_value helper replaces ad-hoc boxing lambdas

        auto direct_call_method1 = [&](const char* name) {
            auto* out = B.CreateAlloca(ValueTy, nullptr, "outv");
            auto decl = M.getOrInsertFunction(name, llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr}, false));
            auto* call = B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {out, selfAlloca});
            (void)call;
            ctx.push_value(B.CreateLoad(ValueTy, out));
        };
        auto direct_call_method2 = [&](const char* name, llvm::Value* arg0BoxPtr) {
            auto* out = B.CreateAlloca(ValueTy, nullptr, "outv");
            auto vArg = B.CreateLoad(ValueTy, arg0BoxPtr);
            auto decl = M.getOrInsertFunction(name, llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, ValueTy}, false));
            auto* call = B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {out, selfAlloca, vArg});
            (void)call;
            ctx.push_value(B.CreateLoad(ValueTy, out));
        };
        auto direct_call_method3 = [&](const char* name, llvm::Value* arg0BoxPtr, llvm::Value* arg1BoxPtr) {
            auto* out = B.CreateAlloca(ValueTy, nullptr, "outv");
            auto decl = M.getOrInsertFunction(name, llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, ValuePtr, ValuePtr}, false));
            auto* call = B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {out, selfAlloca, arg0BoxPtr, arg1BoxPtr});
            (void)call;
            ctx.push_value(B.CreateLoad(ValueTy, out));
        };

        // Evaluate args
        std::vector<llvm::Value*> argvVals;
        for (auto& a : args) { if (a) a->codegen(ctx); argvVals.push_back(ctx.pop_value()); }

        auto lower_string_method = [&]() -> bool {
            if (methodName == "toUpperCase") {
                direct_call_method1("string_to_upper_case");
                return true;
            } else if (methodName == "replace") {
                if (argvVals.size() < 2) return false;
                auto* a0 = box_to_value(ctx, argvVals[0]);
                auto* a1 = box_to_value(ctx, argvVals[1]);
                direct_call_method3("string_replace", a0, a1);
                return true;
            } else if (methodName == "includes") {
                if (argvVals.empty()) return false;
                auto* a0 = box_to_value(ctx, argvVals[0]);
                direct_call_method2("string_includes", a0);
                return true;
            }
            return false;
        };

        // Try string methods first
        if (lower_string_method()) return;

        // Vector methods: push, pop, get, set
        if (methodName == "push") {
            if (argvVals.empty()) { ctx.push_value(llvm::UndefValue::get(ValueTy)); return; }
            auto decl = M.getOrInsertFunction(
                "vector_push_method",
                llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, ValuePtr, ValuePtr}, false)
            );
            auto* out = B.CreateAlloca(ValueTy, nullptr, "outv");
            auto* valBox = box_to_value(ctx, argvVals[0]);
            std::vector<llvm::Value*> callArgs = {out, selfAlloca, valBox};
            B.CreateCall(decl, callArgs);
            ctx.push_value(B.CreateLoad(ValueTy, out));
            return;
        } else if (methodName == "pop") {
            auto decl = M.getOrInsertFunction(
                "vector_pop_method",
                llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, ValuePtr}, false)
            );
            auto* out = B.CreateAlloca(ValueTy, nullptr, "outv");
            B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {out, selfAlloca});
            ctx.push_value(B.CreateLoad(ValueTy, out));
            return;
        } else if (methodName == "get") {
            // expects one integer index
            if (argvVals.size() < 1) { ctx.push_value(llvm::UndefValue::get(ValueTy)); return; }
            auto* I32 = llvm::Type::getInt32Ty(C);
            llvm::Value* idx = argvVals[0];
            if (idx->getType()->isIntegerTy()) {
                if (!idx->getType()->isIntegerTy(32)) idx = B.CreateSExtOrTrunc(idx, I32);
            } else if (idx->getType()->isFloatingPointTy()) {
                idx = B.CreateFPToSI(idx, I32);
            } else {
                idx = llvm::ConstantInt::get(I32, 0);
            }
            auto decl = M.getOrInsertFunction(
                "vector_get_method",
                llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, ValuePtr, I32}, false)
            );
            auto* out = B.CreateAlloca(ValueTy, nullptr, "outv");
            B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {out, selfAlloca, idx});
            ctx.push_value(B.CreateLoad(ValueTy, out));
            return;
        } else if (methodName == "set") {
            // expects index and value
            if (argvVals.size() < 2) { ctx.push_value(llvm::UndefValue::get(ValueTy)); return; }
            auto* I32 = llvm::Type::getInt32Ty(C);
            llvm::Value* idx = argvVals[0];
            if (idx->getType()->isIntegerTy()) {
                if (!idx->getType()->isIntegerTy(32)) idx = B.CreateSExtOrTrunc(idx, I32);
            } else if (idx->getType()->isFloatingPointTy()) {
                idx = B.CreateFPToSI(idx, I32);
            } else {
                idx = llvm::ConstantInt::get(I32, 0);
            }
            auto* valBox = box_to_value(ctx, argvVals[1]);
            auto decl = M.getOrInsertFunction(
                "vector_set_method",
                llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, I32, ValuePtr}, false)
            );
            // no sret for set; it returns void
            {
                std::vector<llvm::Value*> callArgs = {selfAlloca, idx, valBox};
                B.CreateCall(decl, callArgs);
            }
            // no meaningful return; push undef Value
            ctx.push_value(llvm::UndefValue::get(ValueTy));
            return;
        }

        // Array methods (fallback when receiver is an array)
        auto* VTpArr = B.CreateBitCast(protoPtr, llvm::PointerType::getUnqual(ArrayVTableTy));
        if (methodName == "push") {
            if (argvVals.empty()) { ctx.push_value(llvm::UndefValue::get(ValueTy)); return; }
            auto* slot = B.CreateStructGEP(ArrayVTableTy, VTpArr, 0);
            auto* fnptr = B.CreateLoad(Method2Ty, slot);
            auto* out = B.CreateAlloca(ValueTy, nullptr, "outv");
            auto vArg = B.CreateLoad(ValueTy, box_to_value(ctx, argvVals[0]));
            B.CreateCall(Method2FTy, fnptr, {out, selfAlloca, vArg});
            ctx.push_value(B.CreateLoad(ValueTy, out));
            return;
        } else if (methodName == "pop") {
            auto* slot = B.CreateStructGEP(ArrayVTableTy, VTpArr, 1);
            auto* fnptr = B.CreateLoad(Method1Ty, slot);
            auto* out = B.CreateAlloca(ValueTy, nullptr, "outv");
            B.CreateCall(Method1FTy, fnptr, {out, selfAlloca});
            ctx.push_value(B.CreateLoad(ValueTy, out));
            return;
        }

        // Unknown method
        ctx.push_value(llvm::UndefValue::get(ValueTy));
        return;
    }

    // No-op: special-cases are handled explicitly below (write/read)

    std::vector<llvm::Value*> argv;
    argv.reserve(args.size());
    for (auto& a : args) {
        if (a) a->codegen(ctx);
        argv.push_back(ctx.pop_value());
    }

    // Lazy declaration and argument fixups for specific runtime functions
    auto& b = ctx.get_builder();
    auto& M = ctx.get_module();
    auto& C = ctx.get_context();
    if (identName == "write") {
        lower_write_call(ctx, argv);
        ctx.push_value(nullptr);
        return;
    }
    if (identName == "read") {
        // If a prompt argument is provided, print it using runtime write
        if (!args.empty() && argv[0]) {
            emit_rph_write_no_nl(ctx, argv[0]);
        }
        // Ensure declaration exists for rph_read(): i8* ()
        auto decl = M.getOrInsertFunction(
            "rph_read",
            llvm::FunctionType::get(llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(C)), {}, false)
        );
        auto* fn = llvm::cast<llvm::Function>(decl.getCallee());
        auto* call = b.CreateCall(fn, {});
        ctx.push_value(call);
        return;
    }

    // Otherwise, if we know the signature, promote types as needed
    if (callee_ty) {
        for (size_t i = 0; i < argv.size() && i < callee_ty->getNumParams(); ++i) {
            auto* want = callee_ty->getParamType((unsigned)i);
            if (argv[i] && argv[i]->getType() != want) {
                argv[i] = rph::ir_utils::promote_type(ctx, argv[i], want);
            }
        }
    }

    llvm::CallInst* call = nullptr;
    if (callee_fn) {
        call = b.CreateCall(callee_fn, argv);
        callee_ty = callee_fn->getFunctionType();
    } else if (callee_val) {
        // Sem tipo conhecido (opaque pointers). Não é seguro chamar sem assinatura.
        ctx.push_value(nullptr);
        return;
    }

    if (!call) { ctx.push_value(nullptr); return; }
    if (callee_ty && callee_ty->getReturnType()->isVoidTy()) {
        ctx.push_value(nullptr);
        return;
    }
    ctx.push_value(call);
}
