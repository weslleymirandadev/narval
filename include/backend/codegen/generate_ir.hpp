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

// Sistema de tracking para otimizar declarações de funções
struct FeatureTracker {
    bool has_vectors = false;
    bool has_strings = false;
    bool has_maps = false;
    bool has_json = false;
    bool has_read = false;
    bool has_write = false;
    bool has_map_operations = false;
    bool has_string_operations = false;
    bool has_vector_operations = false;
};

// Função para registrar uso de features
void register_feature(const std::string& feature);

// Função para obter o tracker atual
const FeatureTracker& get_feature_tracker();

void generate_ir(std::unique_ptr<Node> node, IRGenerationContext& context, bool keep_result = false);

} // namespace nv