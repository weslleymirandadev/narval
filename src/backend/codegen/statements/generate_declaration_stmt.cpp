#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"

void DeclarationStmtNode::codegen(rph::IRGenerationContext& context) {
    context.set_debug_location(position.get());
    auto* id_node = static_cast<IdentifierNode*>(target.get());
    const std::string& symbol = id_node->symbol;

    std::shared_ptr<rph::Type> rph_type = nullptr;
    llvm::Type* decl_ty = nullptr;

    // Tentar obter tipo inferido do checker se disponível
    if (context.get_type_checker()) {
        auto* checker = static_cast<rph::Checker*>(context.get_type_checker());
        try {
            // Obter tipo inferido/resolvido do checker
            if (typ == "automatic") {
                // Para tipo automático, usar inferência
                rph_type = checker->infer_expr(value.get());
            } else {
                // Para tipo explícito, verificar com o checker
                auto& checked_type = checker->check_node(this);
                rph_type = checked_type;
            }
            
            // Resolver tipo (resolve variáveis de tipo e instancia polimórficos)
            if (rph_type) {
                rph_type = context.resolve_type(rph_type);
                decl_ty = context.rph_type_to_llvm(rph_type);
            }
        } catch (std::exception& e) {
            // Se houver erro no checker, continuar com método tradicional
            // (pode ser que o checker não tenha sido executado ainda)
        }
    }

    // Se não conseguiu obter tipo do checker, usar método tradicional
    if (!decl_ty) {
        decl_ty = rph::ir_utils::llvm_type_from_string(context, typ);
        if (!decl_ty) return;
    }

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
        rph_type,          // Armazenar tipo Ruphi resolvido para uso posterior
        true,              // is_allocated
        constant           // is_constant
    );
    context.get_symbol_table().define_symbol(symbol, info);
}
