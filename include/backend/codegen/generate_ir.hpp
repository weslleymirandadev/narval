#pragma once

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/NoFolder.h>
#include <vector>
#include <memory>
#include <string>
#include "frontend/ast/ast.hpp"
#include "backend/codegen/ir_context.hpp"

namespace nv {

// Rastreia features usadas no código e atributos [...] declarados pelo usuário.
struct FeatureTracker {
    // Features detectadas automaticamente durante codegen
    bool has_vectors           = false;
    bool has_strings           = false;
    bool has_maps              = false;
    bool has_json              = false;
    bool has_read              = false;
    bool has_write             = false;
    bool has_map_operations    = false;
    bool has_string_operations = false;
    bool has_vector_operations = false;

    // [no_std] — remove toda a stdlib do binário final.
    // Apenas o núcleo sintático permanece: criação de literais, aritmética, comparações.
    // Equivalente ao #![no_std] de Rust.
    bool no_std = false;

    // [strip] — passa --strip-all ao linker (remove debug info, menor binário)
    bool strip  = false;
    // [lto]   — habilita link-time optimization (-flto)
    bool lto    = false;

    void apply_attribute(const std::string& attr) {
        if      (attr == "no_std") no_std = true;
        else if (attr == "strip")  strip  = true;
        else if (attr == "lto")    lto    = true;
        // atributos desconhecidos são silenciosamente ignorados
    }

    void apply_decorator(const std::string& decorator) {
        // Por enquanto, decorators são apenas registrados para uso futuro
        // Implementação específica será adicionada conforme necessário
        // decorators desconhecidos são silenciosamente ignorados
    }
};

// Registra uso de uma feature durante o codegen
void register_feature(const std::string& feature);

// Retorna o tracker global atual
const FeatureTracker& get_feature_tracker();

// Acesso mutável — usado pelo pré-scan de generate_ir para setar atributos
FeatureTracker& get_mutable_feature_tracker();

void generate_ir(std::unique_ptr<Node> node, IRGenerationContext& context, bool keep_result = false);

} // namespace nv
