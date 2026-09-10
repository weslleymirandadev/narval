#include "../nir_codegen_utils.hpp"
#include "llvm/Support/raw_ostream.h"
#include "frontend/ast/expressions/identifier_node.hpp"

IdentifierNode::~IdentifierNode() = default;

void IdentifierNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    mlir::Value v = ctx.lookup(symbol);
    if (!v) {
        // The type checker accepted the name but codegen has no binding for it at
        // this point - typically the first assignment sits inside a nested block
        // (a branch), whose scope ends with the block. Say so instead of quietly
        // substituting 0, which turns a scoping mistake into a wrong answer.
        llvm::errs() << ctx.loc(position.get())
                     << "warning: no value bound for '" << symbol
                     << "' here; it is only assigned inside a nested block. Using 0\n";
        v = nir_emit_const(ctx, ctx.loc(position.get()),
                           ctx.get_builder().getI64IntegerAttr(0));
    }
    ctx.push_value(v);
}
