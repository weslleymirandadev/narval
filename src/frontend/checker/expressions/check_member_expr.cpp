#include "frontend/checker/expressions/check_member_expr.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type>& check_member_expr(nv::Checker* ch, Node* node) {
    static thread_local std::shared_ptr<nv::Type> temp_result;
    auto* member_expr = static_cast<MemberExprNode*>(node);
    
    if (!member_expr->object) {
        ch->error(node, "Member expression requires an object");
        return ch->gettyptr("void");
    }
    
    if (!member_expr->property) {
        ch->error(node, "Member expression requires a property");
        return ch->gettyptr("void");
    }
    
    // Verificar tipo do objeto
    auto object_type = ch->infer_expr(member_expr->object.get());
    object_type = ch->unify_ctx.resolve(object_type);
    
    // Verificar que a propriedade é um identificador
    if (member_expr->property->kind != NodeType::Identifier) {
        ch->error(member_expr->property.get(), 
                  "Member expression property must be an identifier");
        return ch->gettyptr("void");
    }
    
    auto* prop_id = static_cast<IdentifierNode*>(member_expr->property.get());
    const std::string& prop_name = prop_id->symbol;
    
    // Verificar se o tipo tem o método/membro
    auto method_type = object_type->get_method(prop_name);
    if (method_type) {
        temp_result = method_type;
        return temp_result;
    }
    
    // Se não encontrou método, verificar se é um tipo com prototype (Map, Tuple, etc.)
    // Por enquanto, apenas reportar erro
    ch->error(member_expr->property.get(), 
              "Type '" + object_type->toString() + "' does not have member '" + prop_name + "'");
    return ch->gettyptr("void");
}
