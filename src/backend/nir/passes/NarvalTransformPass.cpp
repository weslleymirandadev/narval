#include "backend/nir/NarvalPasses.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/PassManager.h"

#include <cstdio>

using namespace mlir;

namespace nv {

void apply_transform_annotations(mlir::ModuleOp module, mlir::PassManager& pm) {
    // Iterate directly over top-level func.func ops — no recursive walk needed.
    for (auto func : module.getOps<func::FuncOp>()) {
        auto opt_attr = func->getAttrOfType<DictionaryAttr>("narval.optimize");
        if (!opt_attr) continue;

        // Attach tile_sizes hint to the function for downstream linalg passes.
        if (auto tile_attr = opt_attr.getAs<DenseI64ArrayAttr>("tile_sizes"))
            func->setAttr("narval.tile_sizes", tile_attr);

        // Attach vectorize hint.
        if (opt_attr.get("vectorize"))
            func->setAttr("narval.vectorize", UnitAttr::get(func->getContext()));

        // Remove the @optimize attribute — processed.
        func->removeAttr("narval.optimize");
    }
}

} // namespace nv
