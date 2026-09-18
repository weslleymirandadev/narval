#pragma once
#include "../types.hpp"
#include <memory>

class MemberExprNode : public Expr {
public:
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> property;
    // Class declaring the method, filled in by the checker from the receiver's static
    // type. The codegen used to look the owner up by method NAME only, which is
    // ambiguous the moment two classes declare the same one (`__str__`, `clone`, ...):
    // it returned "no owner" and the call failed to compile.
    std::string resolved_owner;
    // Non-empty when the receiver is typed by an interface: the class behind the value
    // is only known at run time, so the codegen emits a name-based dispatch
    // (nv_dispatch_method_N) instead of a direct __method_<Class>_<name> call. The value
    // is the interface name, for diagnostics.
    std::string interface_dispatch;
    // The interface declares that method as returning None: the dispatch has no result to
    // keep (the compiled method returns nothing, so the register content is meaningless).
    bool interface_void_result = false;

    MemberExprNode(std::unique_ptr<Expr> object, std::unique_ptr<Expr> property)
        : Expr(NodeType::MemberExpression), object(std::move(object)), property(std::move(property)) {}

    ~MemberExprNode() override;

    Node* clone() const override {
        auto cloned_object = object ? std::unique_ptr<Expr>(static_cast<Expr*>(object->clone())) : nullptr;
        auto cloned_property = property ? std::unique_ptr<Expr>(static_cast<Expr*>(property->clone())) : nullptr;
        auto* node = new MemberExprNode(std::move(cloned_object), std::move(cloned_property));
        node->resolved_owner = resolved_owner;
        node->interface_dispatch = interface_dispatch;
        node->interface_void_result = interface_void_result;
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};

