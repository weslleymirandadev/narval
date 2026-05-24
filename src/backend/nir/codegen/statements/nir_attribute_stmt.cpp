#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/attribute_stmt_node.hpp"

// Parse @[optimize(tile=[M,N,K], vectorize, parallelize, unroll=N)].
// Stores hints in ctx.pending_optimize so the next function can pick them up.
void AttributeStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    if (!has_attr("optimize")) return;

    nv::NIRGenerationContext::OptimizeHints hints;

    for (const auto& entry : entries) {
        if (entry.name == "optimize") continue;  // the outer wrapper

        if (entry.name == "tile") {
            for (const auto& arg : entry.args) {
                try { hints.tile_sizes.push_back(std::stoll(arg.value)); }
                catch (...) {}
            }
        } else if (entry.name == "vectorize") {
            hints.vectorize = true;
        } else if (entry.name == "parallelize") {
            hints.parallelize = true;
        } else if (entry.name == "unroll") {
            if (!entry.args.empty()) {
                try { hints.unroll = std::stoi(entry.args[0].value); }
                catch (...) {}
            }
        }
    }

    // Also check the flattened @[optimize] args list used by the existing parser.
    // Format: @[optimize, tile, M, N, K, vectorize] (positional args after "tile").
    bool next_is_tile = false;
    for (const auto& attr_name : attrs) {
        if (attr_name == "tile")       { next_is_tile = true; continue; }
        if (attr_name == "vectorize")  { hints.vectorize = true; continue; }
        if (attr_name == "parallelize"){ hints.parallelize = true; continue; }
        if (next_is_tile) {
            try { hints.tile_sizes.push_back(std::stoll(attr_name)); }
            catch (...) { next_is_tile = false; }
            continue;
        }
        next_is_tile = false;
    }

    ctx.pending_optimize = hints;
}
