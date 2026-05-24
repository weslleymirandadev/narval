#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/decorator_stmt_node.hpp"

#include <sstream>

// Decorators (@name / @name(args)) precede function definitions.
// The next FunctionStmtNode picks up ctx.pending_optimize and attaches
// the narval.optimize DictionaryAttr to the emitted func.func, then clears it.
//
// Supported forms:
//   @optimize(tile=[16,16,4], vectorize)
//   @optimize { tile(16,16,4); vectorize; unroll(4); }   ← block form
//
void DecoratorStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    for (auto& entry : entries) {
        if (entry.name != "optimize") continue;

        nv::NIRGenerationContext::OptimizeHints hints;
        for (auto& arg : entry.args) {
            // tile=[16,16,4] or tile(16,16,4) — key="tile", value="16,16,4"
            if (arg.key == "tile") {
                std::stringstream ss(arg.value);
                std::string tok;
                while (std::getline(ss, tok, ',')) {
                    try { hints.tile_sizes.push_back(std::stoll(tok)); }
                    catch (...) {}
                }
            }
            // vectorize — key="" value="vectorize" or key="vectorize"
            else if (arg.value == "vectorize" || arg.key == "vectorize") {
                hints.vectorize = true;
            }
            // parallelize — key="" value="parallelize" or key="parallelize"
            else if (arg.value == "parallelize" || arg.key == "parallelize") {
                hints.parallelize = true;
            }
            // unroll=4 or unroll(4)
            else if (arg.key == "unroll") {
                try { hints.unroll = std::stoi(arg.value); }
                catch (...) {}
            }
        }
        ctx.pending_optimize = hints;
    }
}
