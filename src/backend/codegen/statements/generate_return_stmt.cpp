#include "frontend/ast/statements/return_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

void ReturnStmtNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    if (value) {
        value->codegen(ctx);
        auto* v = ctx.pop_value();
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

            nv::ir_utils::create_return(ctx, v);
            return;
        }
    }
    nv::ir_utils::create_return(ctx, nullptr);
}
