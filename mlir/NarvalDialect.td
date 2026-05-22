#ifndef NARVAL_DIALECT_TD
#define NARVAL_DIALECT_TD

include "mlir/IR/DialectBase.td"
include "mlir/IR/OpBase.td"

def Narval_Dialect : Dialect {
    let name = "narval";
    let summary = "Narval language intermediate representation";
    let description = [{
        NIR — the Narval IR dialect. Represents high-level language concepts
        (ownership, boxed values, tensor operations, object field access) before
        progressive lowering to standard MLIR dialects and finally LLVM IR.

        The lowering pipeline is:
          Narval AST
            → narval dialect (this)
            → func / arith / scf / tensor / linalg / memref / vector / gpu
            → llvm dialect
            → LLVM IR
    }];

    let cppNamespace = "::mlir::narval";

    // Enable default type and attribute printer/parser.
    let useDefaultTypePrinterParser = 1;
    let useDefaultAttributePrinterParser = 1;

    // Extra class declarations in the dialect class body.
    let extraClassDeclaration = [{
        void registerTypes();
        void registerOps();
    }];
}

#endif // NARVAL_DIALECT_TD
