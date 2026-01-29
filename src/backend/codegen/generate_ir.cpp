#include "backend/codegen/generate_ir.hpp"
#include "backend/codegen/ir_context.hpp"
#include "frontend/ast/ast.hpp"
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>

namespace rph {

static llvm::StructType* get_or_create_value_ty(llvm::LLVMContext& C) {
    if (auto* T = llvm::StructType::getTypeByName(C, "rph.rt.Value")) return T;
    auto* i32 = llvm::Type::getInt32Ty(C);
    auto* i64 = llvm::Type::getInt64Ty(C);
    auto* i8  = llvm::Type::getInt8Ty(C);
    auto* i8p = llvm::PointerType::getUnqual(i8);
    return llvm::StructType::create(C, {i32, i64, i8p}, "rph.rt.Value");
}

static void declare_runtime(IRGenerationContext& context) {
    auto& M = context.get_module();
    auto& C = context.get_context();
    auto* ValueTy = get_or_create_value_ty(C);
    auto* VoidTy  = llvm::Type::getVoidTy(C);
    auto* I32     = llvm::Type::getInt32Ty(C);
    auto* I64     = llvm::Type::getInt64Ty(C);
    auto* I8      = llvm::Type::getInt8Ty(C);
    auto* I8Ptr   = llvm::PointerType::getUnqual(I8);
    auto* ValuePtr= llvm::PointerType::getUnqual(ValueTy);

    // Object and Array opaque pointers
    auto* ObjPtr  = I8Ptr;
    auto* ArrPtr  = I8Ptr;

    // Prototypes for runtime functions (subset sufficient for stdlib usage)
    // Value helpers (plain Value* out parameters, no sret)
    {
        auto decl = M.getOrInsertFunction("create_str", llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr}, false));
    }
    {
        auto decl = M.getOrInsertFunction("create_float", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::Type::getDoubleTy(C)}, false));
    }
    {
        auto decl = M.getOrInsertFunction("create_int", llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false));
    }
    {
        auto decl = M.getOrInsertFunction("create_bool", llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false));
    }
    {
        auto decl = M.getOrInsertFunction("create_map", llvm::FunctionType::get(VoidTy, {ValuePtr}, false));
    }
    {
        auto decl = M.getOrInsertFunction("create_array", llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false));
    }

    // rph_write(Value*) - função builtin para escrita com nova linha
    M.getOrInsertFunction("rph_write", llvm::FunctionType::get(VoidTy, {llvm::PointerType::getUnqual(ValueTy)}, false));

    // rph_write_no_nl(Value*) - função builtin para escrita sem nova linha
    M.getOrInsertFunction("rph_write_no_nl", llvm::FunctionType::get(VoidTy, {llvm::PointerType::getUnqual(ValueTy)}, false));

    // rph_read() -> i8* - função builtin para leitura
    M.getOrInsertFunction("rph_read", llvm::FunctionType::get(I8Ptr, {}, false));

    // json_load(Value*, const char*) - função builtin para carregar JSON
    M.getOrInsertFunction("json_load", llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr}, false));

    // String methods (names aligned with runtime)
    {
        auto decl = M.getOrInsertFunction("string_to_upper_case", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::PointerType::getUnqual(ValueTy)}, false));
    }
    {
        auto decl = M.getOrInsertFunction("string_replace", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::PointerType::getUnqual(ValueTy), llvm::PointerType::getUnqual(ValueTy), llvm::PointerType::getUnqual(ValueTy)}, false));
    }
    {
        auto decl = M.getOrInsertFunction("string_includes", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::PointerType::getUnqual(ValueTy), ValueTy}, false));
    }

    // Plain C helper used by codegen for string repetition
    {
        M.getOrInsertFunction(
            "string_repeat",
            llvm::FunctionType::get(I8Ptr, { I8Ptr, I32 }, false)
        );
    }

    // Array methods
    {
        auto decl = M.getOrInsertFunction("array_push_method", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::PointerType::getUnqual(ValueTy), ValueTy}, false));
    }
    {
        auto decl = M.getOrInsertFunction("array_pop_method", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::PointerType::getUnqual(ValueTy)}, false));
    }

    // Array wrappers used by codegen for indexing
    {
        // Do NOT mark the first parameter as sret; the C runtime expects a plain pointer.
        M.getOrInsertFunction(
            "array_get_index_v",
            llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::PointerType::getUnqual(ValueTy), I32}, false)
        );
    }
    {
        M.getOrInsertFunction(
            "array_set_index_v",
            llvm::FunctionType::get(VoidTy, {llvm::PointerType::getUnqual(ValueTy), I32, llvm::PointerType::getUnqual(ValueTy)}, false)
        );
    }
}

void generate_ir(
    std::unique_ptr<Node> node,
    IRGenerationContext& context
) {
    auto program = std::unique_ptr<Program>(dynamic_cast<Program*>(node.release()));

    // Ensure runtime declarations are present in the module
    declare_runtime(context);

    context.enter_scope();

    for (size_t i = 0; i < program->body.size(); ++i) {
        program->body[i]->codegen(context);
    }

    context.exit_scope();
}

} // namespace rph