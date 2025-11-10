#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"

void DeclarationStmtNode::codegen(rph::IRGenerationContext& context) {
    auto* id_node = static_cast<IdentifierNode*>(target.get());
    const std::string& symbol = id_node->symbol;

    llvm::Type* decl_ty = rph::ir_utils::llvm_type_from_string(context, typ);
    if (!decl_ty) return;

    // Se houver inicializador, usamos o tipo dele para decidir a alocação.
    // Quando o inicializador já é um Value (ex.: arrays criados via create_array),
    // devemos armazenar como rph.rt.Value para evitar tratá-lo como ponteiro (i8*).
    llvm::AllocaInst* alloca = nullptr;
    llvm::Type* stored_ty = decl_ty;

    llvm::Value* init_val = nullptr;
    if (value) {
        value->codegen(context);
        init_val = context.pop_value();
        if (!init_val) return;

        auto* ValueTy = rph::ir_utils::get_value_struct(context);
        if (init_val->getType() == ValueTy) {
            stored_ty = ValueTy; // caixa de runtime (arrays, objetos, etc.)
        }
    }

    alloca = context.create_alloca(stored_ty, symbol);

    if (init_val) {
        // Promove tipo do valor para o tipo de armazenamento escolhido
        init_val = rph::ir_utils::promote_type(context, init_val, stored_ty);
        if (!init_val) return;
        context.get_builder().CreateStore(init_val, alloca);
    }

    rph::SymbolInfo info(
        alloca,
        stored_ty,
        nullptr,           // rph_type pode ser nullptr por enquanto
        true,              // is_allocated
        constant           // is_constant
    );
    context.get_symbol_table().define_symbol(symbol, info);
}
