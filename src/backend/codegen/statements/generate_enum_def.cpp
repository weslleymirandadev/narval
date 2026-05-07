#include "frontend/ast/statements/enum_def_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>

void EnumDefNode::codegen(nv::IRGenerationContext& ctx) {
    auto& C = ctx.get_context();
    auto& M = ctx.get_module();
    auto& B = ctx.get_builder();

    auto* ValueTy  = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
    auto* I32      = llvm::Type::getInt32Ty(C);
    auto* I8P      = nv::ir_utils::get_i8_ptr(ctx);

    // Create global Value variable for the enum object
    auto* global = M.getGlobalVariable(name);
    if (!global) {
        global = new llvm::GlobalVariable(
            M, ValueTy, false,
            llvm::GlobalValue::ExternalLinkage,
            llvm::Constant::getNullValue(ValueTy),
            name
        );
    }

    // Initialize the enum as a map object at runtime
    auto* global_ptr = B.CreateBitCast(global, ValuePtr);
    auto* create_map_fn = ctx.ensure_runtime_func("create_map", {ValuePtr});
    B.CreateCall(create_map_fn, {global_ptr});

    // Set each variant as an integer field
    auto* set_fn  = ctx.ensure_runtime_func("nv_object_set_field", {ValuePtr, I8P, ValuePtr});
    auto* int_fn  = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});

    int next_value = 0;
    for (const auto& variant : variants) {
        int val = variant.has_explicit_value ? variant.explicit_value : next_value;
        next_value = val + 1;

        auto* slot = ctx.create_alloca(ValueTy, "enum_v_" + variant.name);
        B.CreateCall(int_fn, {slot, llvm::ConstantInt::get(I32, val)});

        auto* key = B.CreateGlobalString(variant.name.c_str(), "enum_k_" + variant.name);
        B.CreateCall(set_fn, {global_ptr, key, slot});
    }

    // Register the enum in the symbol table so identifiers resolve to it
    nv::SymbolInfo info(global, ValueTy, nullptr, false, false);
    ctx.get_symbol_table().define_symbol(name, info);
}
