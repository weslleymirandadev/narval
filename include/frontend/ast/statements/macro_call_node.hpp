#pragma once
#include "../types.hpp"
#include <memory>
#include <string>

// DSL macro invocation: `sql! { SELECT ... }`.
// The body is captured VERBATIM as a string (no parsing) and handed to the
// `comptime def name!(src: str)` macro, which runs at compile time; the checker
// replaces this node with the resulting literal.
class MacroCallNode : public Expr {
public:
    std::string name;     // macro name without the '!'
    std::string raw_src;  // verbatim text between the braces

    MacroCallNode(std::string name, std::string raw_src)
        : Expr(NodeType::MacroCall), name(std::move(name)), raw_src(std::move(raw_src)) {}

    ~MacroCallNode() override = default;

    Node* clone() const override {
        auto* n = new MacroCallNode(name, raw_src);
        if (position) n->position = std::make_unique<PositionData>(*position);
        return n;
    }
};
