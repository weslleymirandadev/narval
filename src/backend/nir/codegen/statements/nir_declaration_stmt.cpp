#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"

void DeclarationStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto  loc = ctx.loc(position.get());

    mlir::Value rhs;
    if (value) {
        value->nir_codegen(ctx);
        rhs = ctx.pop_value();
    }
    if (!rhs)
        rhs = nir_emit_const(ctx, loc, ctx.get_builder().getI64IntegerAttr(0));

    std::string binding;
    if (target && target->kind == NodeType::Identifier)
        binding = static_cast<IdentifierNode*>(target.get())->symbol;

    if (!binding.empty())
        ctx.define(binding, rhs);
}
