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

#endif // NARVAL_TYPES_TD
