#pragma once
#include "frontend/ast/types.hpp"
#include <vector>
#include <string>
#include <memory>

namespace nv { class IRGenerationContext; }

// A single argument to a decorator: @decorator(key=value) or @decorator(value)
struct DecoratorArg {
    std::string key;    // empty if positional
    std::string value;
};

// One decorator application: @name or @name(arg, key=val, ...)
struct DecoratorEntry {
    std::string name;
    std::vector<DecoratorArg> args;

    bool has_arg(const std::string& k) const {
        for (auto& a : args) if (a.key == k || (a.key.empty() && a.value == k)) return true;
        return false;
    }
    std::string get_arg(const std::string& k, const std::string& def = "") const {
        for (auto& a : args) if (a.key == k) return a.value;
        return def;
    }
};

struct DecoratorStmtNode : public Stmt {
    // New structured list (primary).
    std::vector<DecoratorEntry> entries;

    // Legacy flat list kept for backwards-compat with generate_ir.cpp pre-scan.
    std::vector<std::string> decorators;

    DecoratorStmtNode() : Stmt(NodeType::DecoratorStatement) {}

    Node* clone() const override {
        auto* n = new DecoratorStmtNode();
        n->entries    = entries;
        n->decorators = decorators;
        if (position) n->position = std::make_unique<PositionData>(*position);
        return n;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
    void nir_codegen(nv::NIRGenerationContext& ctx) override;
};
