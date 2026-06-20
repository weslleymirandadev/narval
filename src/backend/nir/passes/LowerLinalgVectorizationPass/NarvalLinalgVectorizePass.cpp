// NarvalLinalgVectorizePass.cpp — Fase 5: linalg vectorization.

#include "backend/nir/NarvalPasses.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/Linalg/Transforms/Transforms.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/Vector/IR/VectorOps.h"
#include "mlir/Dialect/Vector/Transforms/LoweringPatterns.h"
#include "mlir/Dialect/Vector/Transforms/VectorTransforms.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#define GEN_PASS_DEF_NARVALLINALGVECTORIZEPASS
#include "NarvalPasses.h.inc"

using namespace mlir;
using namespace mlir::vector;

namespace nv {
namespace {

//===----------------------------------------------------------------------===//
// NarvalLinalgVectorizePass
//===----------------------------------------------------------------------===//

struct NarvalLinalgVectorizePass
    : public ::impl::NarvalLinalgVectorizePassBase<
          NarvalLinalgVectorizePass> {

    void runOnOperation() override {
        ModuleOp module = getOperation();
        IRRewriter rewriter(&getContext());

        for (auto func : module.getOps<func::FuncOp>()) {
            if (func.isExternal() || func.getBody().empty()) continue;
            SmallVector<linalg::LinalgOp> to_vectorize;
            Block& entry = func.getBody().front();
            for (Operation& op : entry.getOperations()) {
                if (auto lop = dyn_cast<linalg::LinalgOp>(&op))
                    to_vectorize.push_back(lop);
            }
            for (linalg::LinalgOp lop : to_vectorize) {
                rewriter.setInsertionPoint(lop);
                // Collect static sizes from the loop bounds (parallel dims only).
                // For linalg.matmul, the vectorization sizes must be the output
                // shape [M, N] — the K reduction dim is handled internally.
                SmallVector<int64_t> vecSizes;
                for (auto [idx, iterType] :
                     llvm::enumerate(lop.getIteratorTypesArray())) {
                    if (iterType == utils::IteratorType::parallel) {
                        auto range = lop.getStaticLoopRanges();
                        if (!range.empty() && idx < range.size() &&
                            range[idx] != ShapedType::kDynamic)
                            vecSizes.push_back(range[idx]);
                    }
                }
                // Only vectorize if we have fully static parallel dims.
                if (!vecSizes.empty()) {
                    (void)linalg::vectorize(rewriter, lop.getOperation(),
                                            vecSizes, {});
                }
            }
        }
    }
};

} // namespace

std::unique_ptr<mlir::Pass> createNarvalLinalgVectorizePass() {
    return std::make_unique<NarvalLinalgVectorizePass>();
}

} // namespace nv
