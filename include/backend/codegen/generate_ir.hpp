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

// Rastreia features usadas no código e atributos @[...] declarados pelo usuário.
struct FeatureTracker {
    // Features detectadas automaticamente durante codegen
    bool has_vectors = false;
    bool has_strings = false;
    bool has_maps = false;
    bool has_read = false;
    bool has_write = false;
    bool has_map_operations = false;
    bool has_string_operations = false;
    bool has_vector_operations = false;

    // Flags de desabilitação — definidas via @[atributo] no topo do arquivo.
    // Impedem a pré-declaração de símbolos no IR e habilitam otimizações de link.
    bool no_exceptions    = false;  // @[no_exceptions] — sem try/catch/throw
    bool no_traceback     = false;  // @[no_traceback]  — sem nv_push/pop_frame
    bool no_write         = false;  // @[no_write]      — sem write()
    bool no_read          = false;  // @[no_read]       — sem read()
    bool no_strings       = false;  // @[no_strings]    — sem métodos de string
    bool no_vectors       = false;  // @[no_vectors]    — sem métodos de vector
    bool no_maps          = false;  // @[no_maps]       — sem métodos de map
    bool no_option_result = false;  // @[no_option_result] — sem Some/None/Ok/Err
    bool minimal          = false;  // @[minimal]       — implica todos os no_* acima
    bool strip            = false;  // @[strip]         — --strip-all no binário final
    bool lto              = false;  // @[lto]           — -flto no link

    // Aplica um atributo textual (chamado pelo codegen de ModuleAttrNode).
    void apply_attribute(const std::string& attr) {
        if (attr == "minimal") {
            minimal = no_exceptions = no_traceback = no_strings =
                      no_vectors = no_maps = no_option_result = true;
        } else if (attr == "no_exceptions")    no_exceptions    = true;
        else if (attr == "no_traceback")       no_traceback     = true;
        else if (attr == "no_write")           no_write         = true;
        else if (attr == "no_read")            no_read          = true;
        else if (attr == "no_strings")         no_strings       = true;
        else if (attr == "no_vectors")         no_vectors       = true;
        else if (attr == "no_maps")            no_maps          = true;
        else if (attr == "no_option_result")   no_option_result = true;
        else if (attr == "strip")              strip            = true;
        else if (attr == "lto")                lto              = true;
    }
};

// Registra uso de uma feature durante o codegen
void register_feature(const std::string& feature);

// Retorna o tracker global atual
const FeatureTracker& get_feature_tracker();

// Acesso mutável — usado pelo codegen de ModuleAttrNode para setar disable flags
FeatureTracker& get_mutable_feature_tracker();

void generate_ir(std::unique_ptr<Node> node, IRGenerationContext& context, bool keep_result = false);

} // namespace nv