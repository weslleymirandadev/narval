#include "frontend/ast/expressions/member_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "backend/codegen/generate_ir.hpp"

void MemberExprNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());

    // Verificar se o objeto é um namespace alias de wildcard import
    if (object->kind == NodeType::Identifier) { auto* obj_id = static_cast<IdentifierNode*>(object.get());
        if (ctx.is_namespace_alias(obj_id->symbol)) {
            std::string member_name;
            if (property->kind == NodeType::Identifier)
                member_name = static_cast<IdentifierNode*>(property.get())->symbol;
            if (!member_name.empty()) {
                auto* ValueTy = nv::ir_utils::get_value_struct(ctx);

                // Tentar resolver o membro diretamente na tabela de símbolos
                auto sym_info = ctx.get_symbol_table().lookup_symbol(member_name);
                if (sym_info.has_value() && sym_info->value) {
                    llvm::Value* val = sym_info->value;
                    if (auto* gv = llvm::dyn_cast<llvm::GlobalVariable>(val)) {
                        val = ctx.get_builder().CreateLoad(ValueTy, gv, member_name + "_load");
                    }
                    ctx.push_value(val);
                    return;
                }

                // Fallback: buscar global ou função com o nome do membro
                auto& M = ctx.get_module();
                if (auto* gv = M.getGlobalVariable(member_name)) {
                    llvm::Value* loaded = ctx.get_builder().CreateLoad(ValueTy, gv, member_name + "_load");
                    ctx.push_value(loaded);
                    return;
                }
                if (auto* fn = M.getFunction(member_name)) {
                    ctx.push_value(fn);
                    return;
                }
            }
        }
    }

    // Registrar features usadas
    nv::register_feature("map");
    nv::register_feature("map_operations");

    // Gerar código para o objeto
    object->codegen(ctx);
    llvm::Value* obj = ctx.pop_value();
    
    // Gerar código para a propriedade (identificador)
    llvm::Value* prop = nullptr;
    if (property->kind == NodeType::Identifier) {
        auto* id = static_cast<IdentifierNode*>(property.get());
        // Criar string para a propriedade
        nv::register_feature("str");
        prop = ctx.get_builder().CreateGlobalString(id->symbol.c_str());
    } else {
        property->codegen(ctx);
        prop = ctx.pop_value();
    }
    
    // Verificar se o objeto é um Map (JSON object)
    auto* ValueTy = nv::ir_utils::get_value_struct(ctx);
    auto* I8P = nv::ir_utils::get_i8_ptr(ctx);
    
    // Use nv_object_get_field: void(Value* out, Value* self, const char* key)
    auto* map_get_fn = ctx.ensure_runtime_func(
        "nv_object_get_field",
        {nv::ir_utils::get_value_ptr(ctx), nv::ir_utils::get_value_ptr(ctx), I8P},
        llvm::Type::getVoidTy(ctx.get_context())
    );
    
    // Criar resultado
    auto* result_alloca = ctx.create_alloca(ValueTy, "member_result");
    
    // Garantir que obj seja um ponteiro para Value
    auto* obj_alloca = ctx.create_alloca(ValueTy, "obj_alloca");
    ctx.get_builder().CreateStore(obj, obj_alloca);
    
    ctx.get_builder().CreateCall(map_get_fn, {result_alloca, obj_alloca, prop});
    
    // Retornar resultado
    llvm::Value* result = ctx.get_builder().CreateLoad(ValueTy, result_alloca);
    ctx.push_value(result);
}
