#include "frontend/ast/statements/return_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

static void pop_and_return(nv::IRGenerationContext& ctx, llvm::Value* v) {
    if (!ctx.get_source_file().empty())
        nv::ir_utils::emit_pop_frame(ctx);
    nv::ir_utils::create_return(ctx, v);
}

void ReturnStmtNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());

    // Inside an `or { }` block, `return expr` sets the or-expression result
    // and branches to the merge block instead of exiting the function.
    if (ctx.in_or_block()) {
        auto& or_ctx = ctx.current_or_block();
        auto& B = ctx.get_builder();
        auto* ValueTy  = nv::ir_utils::get_value_struct(ctx);
        auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);

        if (value) {
            value->codegen(ctx);
            llvm::Value* v = ctx.has_value() ? ctx.pop_value() : nullptr;
            llvm::Value* boxed = nullptr;

            if (v && v->getType() == ValueTy) {
                boxed = v;
            } else if (v) {
                auto* slot = ctx.create_alloca(ValueTy, "or_ret_box");
                auto* I32 = llvm::Type::getInt32Ty(ctx.get_context());
                auto* F64 = llvm::Type::getDoubleTy(ctx.get_context());
                auto* I8P = nv::ir_utils::get_i8_ptr(ctx);
                if (v->getType()->isIntegerTy(1)) {
                    B.CreateCall(ctx.ensure_runtime_func("create_bool", {ValuePtr, I32}),
                                 {slot, B.CreateZExt(v, I32)});
                } else if (v->getType()->isIntegerTy()) {
                    auto* iv = v->getType()->isIntegerTy(32) ? v : B.CreateSExtOrTrunc(v, I32);
                    B.CreateCall(ctx.ensure_runtime_func("create_int", {ValuePtr, I32}), {slot, iv});
                } else if (v->getType()->isFloatingPointTy()) {
                    auto* fv = v->getType() == F64 ? v : B.CreateFPExt(v, F64);
                    B.CreateCall(ctx.ensure_runtime_func("create_float", {ValuePtr, F64}), {slot, fv});
                } else if (v->getType() == I8P) {
                    B.CreateCall(ctx.ensure_runtime_func("create_str", {ValuePtr, I8P}), {slot, v});
                }
                boxed = B.CreateLoad(ValueTy, slot, "or_ret_val");
            }

            if (boxed)
                B.CreateStore(boxed, or_ctx.result_slot);
        }
        B.CreateBr(or_ctx.merge_bb);
        return;
    }

    if (value) {
        value->codegen(ctx);
        auto* v = ctx.pop_value();

        // Função falível: embalar o valor em Result::Ok antes de retornar
        if (v && ctx.is_current_function_fallible() && !ctx.in_or_block()) {
            auto& B        = ctx.get_builder();
            auto* ValueTy  = nv::ir_utils::get_value_struct(ctx);
            auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
            auto* val_slot = ctx.create_alloca(ValueTy, "ok_inner");
            if (v->getType() == ValueTy) {
                B.CreateStore(v, val_slot);
            } else {
                // Alocação temporária para primitivos
                auto* tmp = ctx.create_alloca(ValueTy, "ok_prim");
                auto* I32 = llvm::Type::getInt32Ty(ctx.get_context());
                auto* F64 = llvm::Type::getDoubleTy(ctx.get_context());
                auto* I8P = nv::ir_utils::get_i8_ptr(ctx);
                if (v->getType()->isIntegerTy(1)) {
                    B.CreateCall(ctx.ensure_runtime_func("create_bool", {ValuePtr, I32}),
                                 {tmp, B.CreateZExt(v, I32)});
                } else if (v->getType()->isIntegerTy()) {
                    auto* iv = v->getType()->isIntegerTy(32) ? v : B.CreateSExtOrTrunc(v, I32);
                    B.CreateCall(ctx.ensure_runtime_func("create_int", {ValuePtr, I32}), {tmp, iv});
                } else if (v->getType()->isFloatingPointTy()) {
                    auto* fv = v->getType() == F64 ? v : B.CreateFPExt(v, F64);
                    B.CreateCall(ctx.ensure_runtime_func("create_float", {ValuePtr, F64}), {tmp, fv});
                } else if (v->getType() == I8P) {
                    B.CreateCall(ctx.ensure_runtime_func("create_str", {ValuePtr, I8P}), {tmp, v});
                }
                B.CreateStore(B.CreateLoad(ValueTy, tmp, "ok_prim_val"), val_slot);
            }
            auto* ok_slot = ctx.create_alloca(ValueTy, "ok_result");
            B.CreateCall(ctx.ensure_runtime_func("create_result_ok", {ValuePtr, ValuePtr}),
                         {ok_slot, val_slot});
            v = B.CreateLoad(ValueTy, ok_slot, "ok_val");
        }

        if (v) {
            auto* current_func = ctx.get_current_function();
            if (current_func) {
                auto* ret_type = current_func->getReturnType();
                auto* val_type = v->getType();

                if (val_type != ret_type) {
                    auto& builder = ctx.get_builder();
                    auto* valueStruct = nv::ir_utils::get_value_struct(ctx);
                    auto* valuePtr = nv::ir_utils::get_value_ptr(ctx);
                    auto* i32Ty = nv::ir_utils::get_i32(ctx);
                    auto* f64Ty = nv::ir_utils::get_f64(ctx);

                    // Box primitive into Value if the function returns a Value struct
                    if (ret_type == valueStruct && !val_type->isStructTy()) {
                        auto* outAlloca = ctx.create_alloca(ret_type, "ret_boxed");
                        auto* voidTy = llvm::Type::getVoidTy(ctx.get_context());

                        if (val_type == i32Ty) {
                            auto* fn = ctx.ensure_runtime_func("create_int", {valuePtr, val_type}, voidTy);
                            builder.CreateCall(fn, {outAlloca, v});
                        } else if (val_type == f64Ty) {
                            auto* fn = ctx.ensure_runtime_func("create_float", {valuePtr, val_type}, voidTy);
                            builder.CreateCall(fn, {outAlloca, v});
                        } else if (val_type == nv::ir_utils::get_i1(ctx)) {
                            auto* i32v = builder.CreateZExt(v, i32Ty, "b2i");
                            auto* fn = ctx.ensure_runtime_func("create_bool", {valuePtr, i32Ty}, voidTy);
                            builder.CreateCall(fn, {outAlloca, i32v});
                        } else if (val_type->isPointerTy()) {
                            auto* fn = ctx.ensure_runtime_func("create_str", {valuePtr, val_type}, voidTy);
                            builder.CreateCall(fn, {outAlloca, v});
                        }
                        v = builder.CreateLoad(ret_type, outAlloca, "ret_val");
                    }
                    // Unbox Value into primitive when function expects primitive return
                    else if (val_type == valueStruct && ret_type != valueStruct) {
                        auto* inAlloca = ctx.create_alloca(valueStruct, "ret_unbox_in");
                        builder.CreateStore(v, inAlloca);

                        if (ret_type->isIntegerTy(32)) {
                            auto* fn = ctx.ensure_runtime_func("extract_int_from_value", {valuePtr}, i32Ty);
                            v = builder.CreateCall(fn, {inAlloca}, "ret_int");
                        } else if (ret_type->isFloatingPointTy()) {
                            auto* fn = ctx.ensure_runtime_func("extract_float_from_value", {valuePtr}, f64Ty);
                            auto* fv = builder.CreateCall(fn, {inAlloca}, "ret_float");
                            v = (ret_type == f64Ty) ? fv : builder.CreateFPTrunc(fv, ret_type, "ret_float_cast");
                        } else if (ret_type->isIntegerTy(1)) {
                            auto* fn = ctx.ensure_runtime_func("extract_int_from_value", {valuePtr}, i32Ty);
                            auto* iv = builder.CreateCall(fn, {inAlloca}, "ret_bool_i32");
                            v = builder.CreateICmpNE(iv, llvm::ConstantInt::get(i32Ty, 0), "ret_bool");
                        } else if (ret_type->isPointerTy()) {
                            auto* fn = ctx.ensure_runtime_func("extract_string_from_value", {valuePtr}, ret_type);
                            v = builder.CreateCall(fn, {inAlloca}, "ret_str");
                        }
                    }
                    // Conversão de int para float
                    else if (val_type->isIntegerTy() && ret_type->isFloatingPointTy()) {
                        v = builder.CreateSIToFP(v, ret_type, "int_to_float");
                    }
                    else if (val_type->isFloatingPointTy() && ret_type->isIntegerTy()) {
                        v = builder.CreateFPToSI(v, ret_type, "float_to_int");
                    }
                    else if (val_type->isIntegerTy() && ret_type->isIntegerTy()) {
                        if (val_type->getIntegerBitWidth() < ret_type->getIntegerBitWidth()) {
                            v = builder.CreateSExt(v, ret_type, "int_extend");
                        } else if (val_type->getIntegerBitWidth() > ret_type->getIntegerBitWidth()) {
                            v = builder.CreateTrunc(v, ret_type, "int_truncate");
                        }
                    }
                    else if (val_type->isFloatingPointTy() && ret_type->isFloatingPointTy()) {
                        if (val_type->getScalarSizeInBits() < ret_type->getScalarSizeInBits()) {
                            v = builder.CreateFPExt(v, ret_type, "float_extend");
                        } else if (val_type->getScalarSizeInBits() > ret_type->getScalarSizeInBits()) {
                            v = builder.CreateFPTrunc(v, ret_type, "float_truncate");
                        }
                    }
                }
            }

            pop_and_return(ctx, v);
            return;
        }
    }
    pop_and_return(ctx, nullptr);
}
