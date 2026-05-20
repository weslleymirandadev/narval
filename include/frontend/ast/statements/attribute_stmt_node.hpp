#pragma once
#include "frontend/ast/types.hpp"
#include <vector>
#include <string>
#include <memory>

namespace nv { class IRGenerationContext; }

struct AttrArg {
    std::string value;
};

struct AttrEntry {
    std::string name;
    std::vector<AttrArg> args;
};

struct AttributeStmtNode : public Stmt {
    std::vector<AttrEntry> entries;

    // Compat: lista de nomes simples para código legado (attribute_mapper)
    std::vector<std::string> attrs;

    AttributeStmtNode() : Stmt(NodeType::AttributeStatement) {}

    bool has_attr(const std::string& n) const {
        for (auto& e : entries) if (e.name == n) return true;
        return false;
    }

    std::string get_arg(const std::string& n, size_t idx = 0) const {
        for (auto& e : entries)
            if (e.name == n && idx < e.args.size()) return e.args[idx].value;
        return "";
    }

    Node* clone() const override {
        auto* nd = new AttributeStmtNode();
        nd->entries = entries;
        nd->attrs   = attrs;
        if (position) nd->position = std::make_unique<PositionData>(*position);
        return nd;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};
