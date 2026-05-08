#include "frontend/ast/expressions/new_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

static llvm::Value* box_into_value(nv::IRGenerationContext& ctx, llvm::Value* val) {
    auto& B       = ctx.get_builder();
    auto& C       = ctx.get_context();
    auto& M       = ctx.get_module();
    auto* ValueTy = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
    auto* I32     = llvm::Type::getInt32Ty(C);
    auto* F64     = llvm::Type::getDoubleTy(C);
    auto* I8P     = nv::ir_utils::get_i8_ptr(ctx);

    auto* alloca = ctx.create_alloca(ValueTy, "box");

    if (!val) {
        // nothing
    } else if (val->getType() == ValueTy) {
        B.CreateStore(val, alloca);
    } else if (val->getType()->isIntegerTy(1)) {
        auto* f = ctx.ensure_runtime_func("create_bool", {ValuePtr, I32});
        B.CreateCall(f, {alloca, B.CreateZExt(val, I32)});
    } else if (val->getType()->isIntegerTy()) {
        auto* f = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
        llvm::Value* iv = val->getType()->isIntegerTy(32) ? val : B.CreateSExtOrTrunc(val, I32);
        B.CreateCall(f, {alloca, iv});
    } else if (val->getType()->isFloatingPointTy()) {
        auto* f = ctx.ensure_runtime_func("create_float", {ValuePtr, F64});
        llvm::Value* fp = val->getType() == F64 ? val : B.CreateFPExt(val, F64);
        B.CreateCall(f, {alloca, fp});
    } else if (val->getType() == I8P) {
        auto* f = ctx.ensure_runtime_func("create_str", {ValuePtr, I8P});
        B.CreateCall(f, {alloca, val});
    } else if (val->getType()->isPointerTy()) {
        // Value* pointer — load from it
        auto* loaded = B.CreateLoad(ValueTy, val);
        B.CreateStore(loaded, alloca);
    }

    return alloca; // Value*
}

void NewExprNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    auto& B        = ctx.get_builder();
    auto* ValueTy  = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);

    // Allocate and initialise the object as a map
    auto* this_alloca = ctx.create_alloca(ValueTy, "new_obj");
    auto* create_map_fn = ctx.ensure_runtime_func("create_map", {ValuePtr});
    B.CreateCall(create_map_fn, {this_alloca});

    // Codegen and box each constructor argument
    std::vector<llvm::Value*> ctor_args = {this_alloca};
    for (const auto& arg : arguments) {
        arg->codegen(ctx);
        auto* raw = ctx.pop_value();
        ctor_args.push_back(box_into_value(ctx, raw)); // each is a Value*
    }

    // Call constructor if one was compiled
    std::string ctor_name = "__ctor_" + class_name;
    auto* ctor_fn = ctx.get_module().getFunction(ctor_name);
    if (ctor_fn) {
        B.CreateCall(ctor_fn, ctor_args);
    }

    // Register __str__ method if defined, so nv_str_convert can call it at runtime
    std::string str_fn_name = "__method_" + class_name + "___str__";
    auto* str_fn = ctx.get_module().getFunction(str_fn_name);
    if (str_fn) {
        auto* I8P    = nv::ir_utils::get_i8_ptr(ctx);
        auto* VoidTy = llvm::Type::getVoidTy(ctx.get_context());
        auto* fn_ptr = B.CreatePointerCast(str_fn, I8P, "str_fn_ptr");
        auto* set_fn = ctx.ensure_runtime_func("nv_set_str_method", {ValuePtr, I8P}, VoidTy);
        B.CreateCall(set_fn, {this_alloca, fn_ptr});
    }

    // Store class name on the instance so runtime can display <object:ClassName>
    {
        auto* I8P = nv::ir_utils::get_i8_ptr(ctx);
        auto* class_name_cstr = B.CreateGlobalString(class_name);
        auto* class_name_val = ctx.create_alloca(ValueTy, "class_name_val");
        auto* create_str_fn = ctx.ensure_runtime_func("create_str", {ValuePtr, I8P});
        B.CreateCall(create_str_fn, { class_name_val, class_name_cstr });

        auto* set_field_fn = ctx.ensure_runtime_func("nv_object_set_field", {ValuePtr, I8P, ValuePtr});
        auto* key_ptr = B.CreateGlobalString("__class_name__");
        B.CreateCall(set_field_fn, { this_alloca, key_ptr, class_name_val });
    }

    // Push the resulting object (as Value struct)
    auto* result = B.CreateLoad(ValueTy, this_alloca, "new_result");
    ctx.push_value(result);
}
