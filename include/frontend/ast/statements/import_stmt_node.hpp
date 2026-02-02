#pragma once
#include "../types.hpp"
#include <memory>
#include <string>
#include <vector>

struct ImportItem {
    std::string name;      // Nome do identificador importado
    std::string alias;     // Alias opcional (vazio se não houver)
    size_t line;           // Linha do identificador
    size_t col_start;      // Coluna inicial do identificador (ou alias se houver)
    size_t col_end;        // Coluna final do identificador (ou alias se houver)
    
    ImportItem(const std::string& n, const std::string& a = "", size_t l = 0, size_t cs = 0, size_t ce = 0) 
        : name(n), alias(a), line(l), col_start(cs), col_end(ce) {}
};

class ImportStmtNode : public Stmt {
public:
    std::string module_path;              // Caminho do módulo (ex: "std", "test.nv")
    std::vector<ImportItem> imports;       // Lista de identificadores importados
    std::string filename;                  // Nome do arquivo onde o import está (do token original)
    
    ImportStmtNode(const std::string& module, const std::vector<ImportItem>& items, const std::string& file = "")
        : Stmt(NodeType::ImportStatement), module_path(module), imports(items), filename(file) {}
    
    ~ImportStmtNode() override = default;

    Node* clone() const override {
        auto* node = new ImportStmtNode(this->module_path, this->imports, this->filename);
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};
