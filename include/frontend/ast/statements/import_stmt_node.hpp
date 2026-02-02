#pragma once
#include "../types.hpp"
#include <memory>
#include <string>
#include <vector>

struct ImportItem {
    std::string name;      // Nome do identificador importado
    std::string alias;     // Alias opcional (vazio se não houver)
    
    ImportItem(const std::string& n, const std::string& a = "") 
        : name(n), alias(a) {}
};

class ImportStmtNode : public Stmt {
public:
    std::string module_path;              // Caminho do módulo (ex: "std", "test.nv")
    std::vector<ImportItem> imports;       // Lista de identificadores importados
    
    ImportStmtNode(const std::string& module, const std::vector<ImportItem>& items)
        : Stmt(NodeType::ImportStatement), module_path(module), imports(items) {}
    
    ~ImportStmtNode() override = default;

    Node* clone() const override {
        auto* node = new ImportStmtNode(this->module_path, this->imports);
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};
