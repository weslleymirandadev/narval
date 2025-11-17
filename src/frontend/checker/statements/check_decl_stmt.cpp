#include "frontend/checker/statements/check_decl_stmt.hpp"
#include "frontend/ast/ast.hpp"
#include <stdexcept>

std::shared_ptr<rph::Type>& check_decl_stmt(rph::Checker* ch, Node* node) {
    auto* decl = static_cast<DeclarationStmtNode*>(node);
    auto* name = static_cast<IdentifierNode*>(decl->target.get());
    auto& vtype = ch->check_node(decl->value.get());
    if (decl->typ == "automatic") {
        ch->scope->put_key(name->symbol, vtype, decl->constant);
        return vtype;
    } else {
        auto& dtype = ch->getty(decl->typ);
        if (!dtype->equals(*vtype)) {
            throw std::runtime_error(std::string("Expected type '") + dtype->toString() + "', but got '" + vtype->toString() + "'.");
        }
        ch->scope->put_key(name->symbol, dtype, decl->constant);
    }
    return vtype;
}