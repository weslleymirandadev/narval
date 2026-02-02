#include "frontend/ast/statements/return_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

void ReturnStmtNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    if (value) {
        value->codegen(ctx);
        auto* v = ctx.pop_value();
        if (v) {
            // Obter o tipo de retorno da função atual
            auto* current_func = ctx.get_current_function();
            if (current_func) {
                auto* ret_type = current_func->getReturnType();
                auto* val_type = v->getType();
                
                // Se o tipo do valor não corresponde ao tipo de retorno, converter
                if (val_type != ret_type) {
                    auto& builder = ctx.get_builder();
                    
                    // Conversão de int para float
                    if (val_type->isIntegerTy() && ret_type->isFloatingPointTy()) {
                        v = builder.CreateSIToFP(v, ret_type, "int_to_float");
                    }
                    // Conversão de float para int (se necessário)
                    else if (val_type->isFloatingPointTy() && ret_type->isIntegerTy()) {
                        v = builder.CreateFPToSI(v, ret_type, "float_to_int");
                    }
                    // Conversão de tipos inteiros de tamanhos diferentes
                    else if (val_type->isIntegerTy() && ret_type->isIntegerTy()) {
                        if (val_type->getIntegerBitWidth() < ret_type->getIntegerBitWidth()) {
                            v = builder.CreateSExt(v, ret_type, "int_extend");
                        } else if (val_type->getIntegerBitWidth() > ret_type->getIntegerBitWidth()) {
                            v = builder.CreateTrunc(v, ret_type, "int_truncate");
                        }
                    }
                    // Conversão de tipos float de tamanhos diferentes
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
