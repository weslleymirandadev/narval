#pragma once

// tensor_nir.hpp — MLIR bridge for tensor operations.
//
// Caminho B: for each tensor binary op, builds a specialised MLIR kernel
// using SCF loops + LLVM dialect, lowers through the NIR pipeline, and links
// the resulting llvm::Module into the main IRGenerationContext module.
// Falls back to the C runtime if NARVAL_USE_NIR is off or lowering fails.

#ifdef NARVAL_USE_NIR

#include "backend/codegen/ir_context.hpp"
#include <llvm/IR/Value.h>
#include <cstdint>
#include <string>
#include <vector>

namespace nv {

// Emit MLIR matmul kernel: C[M,N] = A[M,K] @ B[K,N].
// lhs_dims / rhs_dims carry the static shape from the checker.
// Returns the result Value struct, or nullptr → caller uses C runtime fallback.
llvm::Value* emit_tensor_matmul_nir(IRGenerationContext& ctx,
                                     llvm::Value* lhs_v,
                                     llvm::Value* rhs_v,
                                     const std::vector<int64_t>& lhs_dims,
                                     const std::vector<int64_t>& rhs_dims);

// Emit MLIR element-wise kernel: C[i] = A[i] OP B[i].
// op: "add" | "sub" | "mul"
// dims: shape of either operand (they must match).
llvm::Value* emit_tensor_elemwise_nir(IRGenerationContext& ctx,
                                       llvm::Value* lhs_v,
                                       llvm::Value* rhs_v,
                                       const std::vector<int64_t>& dims,
                                       const std::string& op);

} // namespace nv

#endif // NARVAL_USE_NIR
