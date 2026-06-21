#pragma once

#include "frontend/ast/types.hpp"
#include "mlir/IR/Value.h"

namespace nv {
class NIRGenerationContext;
} // namespace nv

namespace nv_tensor_codegen {

// ── High-level dispatch ───────────────────────────────────────────────
// Called from nir_call_expr.cpp / nir_member_expr.cpp.
// Handles ALL Tensor-related AST patterns internally.
// Returns true if handled, false to fall through.

// Handle: Tensor(dims), Tensor.zeros(dims), Tensor.ones(dims),
//         obj.item(), obj.tolist(), obj.reshape(dims)
// Pass the CallExprNode* as Node*
bool try_handle_call(nv::NIRGenerationContext& ctx, Node* call_expr);

// Handle: obj.shape, obj.ndim, obj.dtype, obj.T, obj.size
// For member expression on tensor values.
bool try_handle_member(nv::NIRGenerationContext& ctx,
                       mlir::Value object_val,
                       const std::string& property);

} // namespace nv_tensor_codegen
