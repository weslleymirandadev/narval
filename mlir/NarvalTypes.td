#ifndef NARVAL_TYPES_TD
#define NARVAL_TYPES_TD

include "mlir/IR/BuiltinTypes.td"
include "mlir/IR/AttrTypeBase.td"
include "NarvalDialect.td"

// Base class for all Narval types

class Narval_Type<string name, string typeMnemonic, list<Trait> traits = []>
    : TypeDef<Narval_Dialect, name, traits> {
    let mnemonic = typeMnemonic;
}

def Narval_ValueType : Narval_Type<"Value", "value"> {
    let summary = "Boxed Narval runtime value (wraps NvObject*)";
    let description = [{
        The universal boxed value type of Narval. At runtime this is a struct
        containing a single pointer to an NvTypeObject tagged with a type id.

        Equivalent to the existing `nv.rt.Value.v2 = { ptr }` struct in LLVM IR.
        All runtime operations (arith, comparisons, field access) operate on this
        type via the Narval C runtime API.
    }];
}

// !narval.ref<T> — immutable borrowed reference

// Both types lower to ptr (same as !narval.value) for now.
def Narval_RefType : Narval_Type<"Ref", "ref"> {
    let summary = "Immutable borrowed reference (Phase 1: ptr alias)";
}

// !narval.mut_ref — mutable borrowed reference (Phase 1: unparametrized)

def Narval_MutRefType : Narval_Type<"MutRef", "mut_ref"> {
    let summary = "Mutable borrowed reference (Phase 1: ptr alias)";
}

//===----------------------------------------------------------------------===//
// Phase 3+: Parameterized types
//===----------------------------------------------------------------------===//

// !narval.tensor<elementType, [d0, d1, ...]>
// Phase-4 type. shape values: -1 = dynamic dimension.
// Lowers to mlir::RankedTensorType after bufferization.
def Narval_TensorType : Narval_Type<"Tensor", "tensor"> {
    let summary = "Narval static tensor with known element type and shape";
    let description = [{
        Dense tensor with statically-known element type and shape.
        Dimensions unknown at compile time are represented as -1 (dynamic).
        Used by the tensor/linalg pipeline (Phase 4).
        Lowers to mlir::RankedTensorType after bufferization.
    }];
    let parameters = (ins
        "::mlir::Type":$elementType,
        ArrayRefParameter<"int64_t", "shape dimensions (-1 = dynamic)">:$shape
    );
    let assemblyFormat = "`<` $elementType `,` `[` $shape `]` `>`";
}

// !narval.class<"ClassName">
// NVMap-backed class instance. Lowers to !narval.value (opaque runtime ptr).
def Narval_ClassType : Narval_Type<"Class", "class"> {
    let summary = "Narval class instance (NVMap-backed)";
    let description = [{
        Represents a runtime instance of a named Narval class. Backed by an
        NVMap whose keys are the field names. Lowers to !narval.value (opaque
        pointer) for the existing runtime get_field / set_field API.
    }];
    let parameters = (ins "::mlir::StringAttr":$className);
    let assemblyFormat = "`<` $className `>`";
}

// !narval.option<T>
// None | Some(T). Runtime: NV_OPTION_NONE_BASE (21) / NV_OPTION_SOME_BASE (22).
def Narval_OptionType : Narval_Type<"Option", "option"> {
    let summary = "Optional value: None | Some(T)";
    let description = [{
        Optional value type. At runtime uses NV_OPTION_NONE_BASE (21) and
        NV_OPTION_SOME_BASE (22) type tags inside !narval.value.
        Phase-1 lowering: treated as !narval.value (runtime handles the tag).
    }];
    let parameters = (ins "::mlir::Type":$valueType);
    let assemblyFormat = "`<` $valueType `>`";
}

// !narval.result<T, E>
// Ok(T) | Err(E). Runtime: NV_RESULT_OK_BASE (23) / NV_RESULT_ERR_BASE (24).
def Narval_ResultType : Narval_Type<"Result", "result"> {
    let summary = "Fallible result: Ok(T) | Err(E)";
    let description = [{
        Represents a fallible computation result. At runtime uses
        NV_RESULT_OK_BASE (23) and NV_RESULT_ERR_BASE (24) type tags.
        Used by fallible functions (those containing propagate statements).
    }];
    let parameters = (ins "::mlir::Type":$okType, "::mlir::Type":$errType);
    let assemblyFormat = "`<` $okType `,` $errType `>`";
}

// !narval.future<T>
// Async computation result. Phase-5+ lowering; for now treated as !narval.value.
def Narval_FutureType : Narval_Type<"Future", "future"> {
    let summary = "Async future value";
    let description = [{
        Pending result of an async computation. Used by functions marked with
        is_async. Phase-5+ lowering; currently treated as !narval.value.
    }];
    let parameters = (ins "::mlir::Type":$valueType);
    let assemblyFormat = "`<` $valueType `>`";
}

#endif // NARVAL_TYPES_TD
