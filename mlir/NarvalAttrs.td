#ifndef NARVAL_ATTRS_TD
#define NARVAL_ATTRS_TD

include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/BuiltinAttributeInterfaces.td"
include "NarvalDialect.td"

class Narval_Attr<string name, string attrMnemonic, list<Trait> traits = []>
    : AttrDef<Narval_Dialect, name, traits> {
    let mnemonic = attrMnemonic;
}

//===----------------------------------------------------------------------===//
// NarvalOptimizeAttr — #narval.optimize<...>
//
// Attached to narval.func as the $optimize attribute. Parsed from
// @optimize { ... } blocks by the attribute checker and consumed by
// NarvalTransformPass to emit Transform Dialect IR.
//===----------------------------------------------------------------------===//

def Narval_OptimizeAttr : Narval_Attr<"Optimize", "optimize"> {
    let summary = "Structured optimization hints from @optimize { ... } annotations";
    let description = [{
        Holds the optimization directives from @optimize { } blocks.
        NarvalTransformPass reads this attribute and emits the equivalent
        Transform Dialect IR (tile_using_forall, vectorize, unroll, etc.).

        Example (Narval source):
            @optimize {
                tile(16, 16, 4)
                vectorize
                unroll(4)
                parallelize(axis: 0)
            }
            def matmul(a: Tensor<float, [512, 512]>, b: ...): ... { ... }

        The generated attribute on narval.func:
            #narval.optimize<tile_sizes = [16, 16, 4], vectorize = true,
                             unroll_factor = 4, parallelize_axis = 0>
    }];
    let parameters = (ins
        OptionalParameter<"::mlir::DenseI64ArrayAttr",
                          "tile sizes for each loop dimension">:$tile_sizes,
        DefaultValuedParameter<"bool", "false">:$vectorize,
        OptionalParameter<"std::optional<int64_t>",
                          "loop unroll factor">:$unroll_factor,
        OptionalParameter<"std::optional<int64_t>",
                          "axis to parallelize over">:$parallelize_axis
    );
    // Generic MLIR attribute format: #narval.optimize<...>
    let hasCustomAssemblyFormat = 0;
}

#endif // NARVAL_ATTRS_TD
