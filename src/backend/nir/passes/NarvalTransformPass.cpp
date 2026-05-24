// NarvalTransformPass.cpp — Fase 7: @optimize hints → tiling + vectorization.
//
// Reads the narval.optimize DictionaryAttr attached by nir_function_stmt.cpp
// (populated from @[optimize(...)] attributes) and applies the requested
// transformations directly using the MLIR linalg C++ API:
//   - tile_sizes  → linalg::tileLinalgOp  (tiling for cache efficiency)
//   - vectorize   → linalg::vectorize     (SIMD via vector dialect)
//
// This is semantically equivalent to generating a Transform Dialect script and
// applying it, without the MLIR-version-specific IR construction complexity.

#include "backend/nir/NarvalPasses.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/Linalg/Transforms/Transforms.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

using namespace mlir;

namespace nv {

void apply_transform_annotations(mlir::ModuleOp module,
                                  mlir::PassManager& /*pm*/) {
    MLIRContext* ctx = module.getContext();

    SmallVector<func::FuncOp> funcs(module.getOps<func::FuncOp>());
    for (auto func : funcs) {
        auto opt_attr = func->getAttrOfType<DictionaryAttr>("narval.optimize");
        if (!opt_attr) continue;

        // ── Parse hints ────────────────────────────────────────────────────
        SmallVector<int64_t> tile_sizes;
        bool do_vectorize = false;
        int  unroll       = 0;

        if (auto ts = opt_attr.getAs<DenseI64ArrayAttr>("tile_sizes"))
            tile_sizes = SmallVector<int64_t>(ts.asArrayRef());
        if (opt_attr.get("vectorize"))
            do_vectorize = true;
        if (auto u = opt_attr.getAs<IntegerAttr>("unroll"))
            unroll = static_cast<int>(u.getInt());

        (void)unroll; // loop unrolling handled by downstream LLVM pass

        // ── Collect linalg ops in the function ─────────────────────────────
        SmallVector<linalg::LinalgOp> linalg_ops;
        func.walk([&](linalg::LinalgOp op) { linalg_ops.push_back(op); });

        if (linalg_ops.empty()) {
            func->removeAttr("narval.optimize");
            continue;
        }

        IRRewriter rewriter(ctx);

        // ── Tiling ─────────────────────────────────────────────────────────
        if (!tile_sizes.empty()) {
            for (linalg::LinalgOp op : linalg_ops) {
                if (!op->getBlock()) continue;  // erased in a previous iteration

                // Pad tile_sizes to match the op's loop count (0 = don't tile).
                unsigned n_loops = op.getNumLoops();
                SmallVector<int64_t> sizes(n_loops, 0);
                for (unsigned i = 0; i < tile_sizes.size() && i < n_loops; ++i)
                    sizes[i] = tile_sizes[i];

                linalg::LinalgTilingOptions opts;
                opts.setTileSizes(sizes);

                rewriter.setInsertionPoint(op);
                auto tiled = linalg::tileLinalgOp(rewriter, op, opts);
                if (succeeded(tiled)) {
                    rewriter.replaceOp(op, tiled->tensorResults);
                }
            }
        }

        // ── Vectorization ──────────────────────────────────────────────────
        if (do_vectorize) {
            // Re-collect after tiling (ops may have changed).
            SmallVector<linalg::LinalgOp> to_vectorize;
            func.walk([&](linalg::LinalgOp op) { to_vectorize.push_back(op); });

            for (linalg::LinalgOp op : to_vectorize) {
                if (!op->getBlock()) continue;

                // Collect static parallel-dim sizes for vectorization.
                SmallVector<int64_t> vec_sizes;
                for (auto [idx, iter_type] :
                     llvm::enumerate(op.getIteratorTypesArray())) {
                    if (iter_type == utils::IteratorType::parallel) {
                        auto ranges = op.getStaticLoopRanges();
                        if (idx < ranges.size() &&
                            ranges[idx] != ShapedType::kDynamic)
                            vec_sizes.push_back(ranges[idx]);
                    }
                }

                rewriter.setInsertionPoint(op);
                if (!vec_sizes.empty())
                    (void)linalg::vectorize(rewriter, op.getOperation(),
                                            vec_sizes, {});
            }
        }

        func->removeAttr("narval.optimize");
    }
}

} // namespace nv
