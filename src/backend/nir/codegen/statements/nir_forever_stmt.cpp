#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/forever_stmt_node.hpp"
#include "frontend/ast/statements/while_stmt_node.hpp"
#include "frontend/ast/expressions/boolean_literal_node.hpp"
#include "mlir/Dialect/Arith/IR/Arith.h"

// `forever { body }` — infinite loop; lowers to narval.while(true).
void ForeverStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    // `forever { body }` is `while (true) { body }`. Emitting it as a while is what gives the body
    // its loop-carried values: a variable assigned inside the loop is threaded through the loop's
    // results and redefined after it. Emitting the body directly dropped the assignment into a
    // scope that was popped at the end of each iteration, so the next iteration read the value
    // from before the loop — `n += 1` inside a forever printed 1 forever.
    CodeBlock body_copy;
    body_copy.reserve(body.size());
    for (const auto& stmt : body)
        if (stmt) body_copy.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));

    auto condition = std::make_unique<BooleanLiteralNode>(true);
    auto while_node = std::make_unique<WhileStmtNode>(std::move(condition), std::move(body_copy));
    if (position)
        while_node->position = std::make_unique<PositionData>(*position);
    while_node->nir_codegen(ctx);
}
