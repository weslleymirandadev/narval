# NIR — Narval Intermediate Representation (MLIR-based)

## 1. Visão Geral

O NIR (Narval IR) é um dialeto MLIR próprio da linguagem Narval. Em vez de compilar
`AST → LLVM IR` diretamente (como faz hoje o `IRGenerationContext`), o pipeline passa por:

```
Narval AST
  → NIR (narval dialect)          ← conceitos de alto nível da linguagem
  → Standard Dialects             ← tensor / linalg / memref / arith / scf / func
  → LLVM Dialect                  ← representação LLVM dentro do MLIR
  → LLVM IR                       ← saída final (igual a hoje)
  → Binary
```

Isso permite:
- Representar ownership/borrow antes de decidir sobre memória
- Aplicar otimizações de tensores (tiling, fusion, vectorization) antes de baixar para LLVM
- Emitir kernels GPU sem sair do mesmo pipeline
- Criar passes de canonicalização e pattern-rewriting próprios da Narval
- Usar o Transform Dialect para otimizações controláveis via anotações no código

O MLIR já está compilado com suporte a MLIR — este documento descreve como integrá-lo.

---

## 2. Estrutura de Diretórios

```
narval/
├── mlir/                              ← definições TableGen
│   ├── NarvalDialect.td
│   ├── NarvalOps.td
│   ├── NarvalTypes.td
│   ├── NarvalAttrs.td
│   └── NarvalPasses.td
├── include/backend/nir/
│   ├── NarvalDialect.h               ← gerado + mão
│   ├── NarvalOps.h                   ← gerado
│   ├── NarvalTypes.h                 ← gerado
│   ├── NarvalAttrs.h                 ← gerado
│   ├── NarvalPasses.h                ← gerado
│   └── NIRGenerationContext.hpp      ← substitui IRGenerationContext para o path NIR
└── src/backend/nir/
    ├── NarvalDialect.cpp
    ├── NarvalOps.cpp
    ├── NarvalTypes.cpp
    ├── NIRGenerationContext.cpp
    └── passes/
        ├── LowerNarvalToStandard.cpp      ← narval → arith/scf/func/tensor
        ├── LowerTensorToLinalg.cpp        ← narval.tensor_* → linalg.*
        ├── LowerOwnershipToMemRef.cpp     ← narval.borrow/move → memref
        ├── NarvalCanonicalization.cpp     ← fold/simplify patterns
        ├── LowerLinalgToLoops.cpp         ← linalg → scf.for
        ├── LowerVectorToLLVM.cpp          ← vector → LLVM vector ops (SIMD)
        ├── LowerGPUKernels.cpp            ← narval.gpu_kernel → gpu dialect
        └── LowerToLLVM.cpp               ← LLVM Dialect → LLVM IR final
```

---

## 3. CMakeLists.txt — Integração MLIR

```cmake
# Encontrar MLIR
find_package(MLIR REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")
include(AddMLIR)
include(HandleLLVMOptions)

# Dialects MLIR usados
set(MLIR_DEPS
    MLIRArithDialect
    MLIRFuncDialect
    MLIRSCFDialect
    MLIRAffineDialect
    MLIRTensorDialect
    MLIRLinalgDialect
    MLIRMemRefDialect
    MLIRVectorDialect
    MLIRGPUDialect
    MLIRTransformDialect
    MLIRTransforms
    MLIRConversionPasses
    MLIRLLVMDialect
    MLIRLLVMToLLVMIRTranslation
    MLIRToLLVMIRTranslation
    MLIRBufferizationDialect
    MLIRBufferizationTransforms
    MLIRLinalgTransforms
)

# Geração TableGen
set(LLVM_TARGET_DEFINITIONS mlir/NarvalOps.td)
mlir_tablegen(NarvalOps.h.inc      -gen-op-decls)
mlir_tablegen(NarvalOps.cpp.inc    -gen-op-defs)
mlir_tablegen(NarvalDialect.h.inc  -gen-dialect-decls)
mlir_tablegen(NarvalDialect.cpp.inc -gen-dialect-defs)
mlir_tablegen(NarvalTypes.h.inc    -gen-typedef-decls)
mlir_tablegen(NarvalTypes.cpp.inc  -gen-typedef-defs)
mlir_tablegen(NarvalAttrs.h.inc    -gen-attrdef-decls)
mlir_tablegen(NarvalAttrs.cpp.inc  -gen-attrdef-defs)
add_public_tablegen_target(NarvalDialectIncGen)

set(LLVM_TARGET_DEFINITIONS mlir/NarvalPasses.td)
mlir_tablegen(NarvalPasses.h.inc   -gen-pass-decls --name Narval)
add_public_tablegen_target(NarvalPassesIncGen)

# Target principal (adicionar src/backend/nir/ no GLOB_RECURSE existente)
target_link_libraries(narval PRIVATE ${MLIR_DEPS})
target_include_directories(narval PRIVATE
    ${MLIR_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}/include  # para os .inc gerados
)
```

---

## 4. Narval Dialect — TableGen

### `mlir/NarvalDialect.td`

```tablegen
include "mlir/IR/DialectBase.td"

def Narval_Dialect : Dialect {
    let name = "narval";
    let summary = "Narval language IR dialect";
    let description = [{
        NIR — dialeto MLIR da linguagem Narval.
        Representa conceitos de alto nível (ownership, tensores, classes, error handling)
        antes de baixar para dialetos padrão do MLIR.
    }];
    let cppNamespace = "::mlir::narval";

    // Tipos customizados registrados neste dialect
    let useDefaultTypePrinterParser = 1;
    let useDefaultAttributePrinterParser = 1;
}
```

---

## 5. NIR Types

### `mlir/NarvalTypes.td`

```tablegen
include "mlir/IR/AttrTypeBase.td"
include "NarvalDialect.td"

class Narval_Type<string name, string typeMnemonic>
    : TypeDef<Narval_Dialect, name> {
    let mnemonic = typeMnemonic;
}

// Valor boxed Narval — equivalente ao atual nv.rt.Value.v2 = { ptr }
// Usado em todo o código Narval de propósito geral
def Narval_ValueType : Narval_Type<"Value", "value"> {
    let summary = "Boxed Narval runtime value (wraps NvObject*)";
    // Lowering: { ptr } no LLVM Dialect
}

// Tensor estático com dimensões conhecidas em compile-time
// Equivale a Tensor<float32, [M, N]> da linguagem
def Narval_TensorType : Narval_Type<"Tensor", "tensor"> {
    let summary = "Narval static tensor with known shape";
    let parameters = (ins
        "::mlir::Type":$elementType,
        "::llvm::ArrayRef<int64_t>":$shape   // -1 = dinâmico
    );
    let assemblyFormat = "`<` $elementType `,` `[` $shape `]` `>`";
    // Lowering: mlir::TensorType ou mlir::MemRefType após bufferização
}

// Referência emprestada imutável (borrow)
def Narval_RefType : Narval_Type<"Ref", "ref"> {
    let summary = "Immutable borrowed reference";
    let parameters = (ins "::mlir::Type":$pointeeType);
    let assemblyFormat = "`<` $pointeeType `>`";
    // Lowering: memref<?x elementType> ou ptr (dependendo do objeto)
}

// Referência emprestada mutável
def Narval_MutRefType : Narval_Type<"MutRef", "mut_ref"> {
    let summary = "Mutable borrowed reference";
    let parameters = (ins "::mlir::Type":$pointeeType);
    let assemblyFormat = "`<` $pointeeType `>`";
}

// Option<T>
def Narval_OptionType : Narval_Type<"Option", "option"> {
    let parameters = (ins "::mlir::Type":$valueType);
    let assemblyFormat = "`<` $valueType `>`";
    // Lowering: narval.value (usa NV_OPTION_NONE_BASE / NV_OPTION_SOME_BASE em runtime)
}

// Result<T, E>
def Narval_ResultType : Narval_Type<"Result", "result"> {
    let parameters = (ins "::mlir::Type":$okType, "::mlir::Type":$errType);
    let assemblyFormat = "`<` $okType `,` $errType `>`";
}

// Tipo de classe (instância de objeto Narval)
def Narval_ClassType : Narval_Type<"Class", "class"> {
    let parameters = (ins "::mlir::StringAttr":$className);
    let assemblyFormat = "`<` $className `>`";
    // Lowering: narval.value (map-backed NVMap)
}

// Future<T> para async/await
def Narval_FutureType : Narval_Type<"Future", "future"> {
    let parameters = (ins "::mlir::Type":$valueType);
    let assemblyFormat = "`<` $valueType `>`";
}
```

---

## 6. NIR Operations

### `mlir/NarvalOps.td`

```tablegen
include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "mlir/Interfaces/CallInterfaces.td"
include "mlir/Interfaces/ControlFlowInterfaces.td"
include "NarvalDialect.td"
include "NarvalTypes.td"

class Narval_Op<string mnemonic, list<Trait> traits = []>
    : Op<Narval_Dialect, mnemonic, traits>;
```

#### 6.1 Funções e Chamadas

```tablegen
// Definição de função Narval
// Análogo ao func.func mas com metadados de ownership e ABI da Narval
def Narval_FuncOp : Narval_Op<"func", [
    IsolatedFromAbove, Symbol, CallableOpInterface
]> {
    let summary = "Narval function definition";
    let arguments = (ins
        SymbolNameAttr:$sym_name,
        TypeAttrOf<FunctionType>:$function_type,
        OptionalAttr<StrAttr>:$abi,            // "sysv64", "win64", "C", ""
        UnitAttr:$is_fallible,                 // envolve return em Result
        UnitAttr:$is_async,                    // retorna Future<T>
        UnitAttr:$is_comptime                  // avaliada em compile-time
    );
    let regions = (region AnyRegion:$body);
    // Lowering: func.func
}

// Chamada de função Narval
def Narval_CallOp : Narval_Op<"call"> {
    let arguments = (ins
        FlatSymbolRefAttr:$callee,
        Variadic<AnyType>:$operands
    );
    let results = (outs Variadic<AnyType>:$results);
    // Lowering: func.call
}

// Return
def Narval_ReturnOp : Narval_Op<"return", [Terminator]> {
    let arguments = (ins Variadic<AnyType>:$operands);
    // Lowering: func.return
}
```

#### 6.2 Ownership e Memória

```tablegen
// Aloca um valor Narval (runtime: nv_alloc_*)
def Narval_AllocOp : Narval_Op<"alloc"> {
    let summary = "Allocate a new Narval value on the heap";
    let arguments = (ins StrAttr:$runtime_ctor);   // ex: "nv_alloc_int"
    let results = (outs Narval_ValueType:$result);
    // Lowering: call runtime alloc + store tag
}

// Move de ownership — o valor original não pode mais ser usado
def Narval_MoveOp : Narval_Op<"move", [Pure]> {
    let summary = "Transfer ownership of a value";
    let arguments = (ins AnyType:$source);
    let results = (outs AnyType:$result);
    // Lowering: identity (sem custo em runtime — apenas análise estática)
    // O pass de ownership analysis usa isso para invalidar o source no scope
}

// Borrow imutável
def Narval_BorrowOp : Narval_Op<"borrow", [Pure]> {
    let summary = "Create an immutable borrow of a value";
    let arguments = (ins AnyType:$source);
    let results = (outs Narval_RefType:$result);
    // Lowering: getelementptr + ptr (sem cópia)
}

// Borrow mutável
def Narval_BorrowMutOp : Narval_Op<"borrow_mut"> {
    let summary = "Create a mutable borrow of a value";
    let arguments = (ins AnyType:$source);
    let results = (outs Narval_MutRefType:$result);
    // Lowering: ptr direto — análise garante exclusividade
}

// Drop explícito (normalmente inferido; inserido pelo ownership pass)
def Narval_DropOp : Narval_Op<"drop"> {
    let summary = "Release ownership and run destructor";
    let arguments = (ins Narval_ValueType:$value);
    // Lowering: call nv_free(value) ou __dtor_ClassName(value)
}
```

#### 6.3 Controle de Fluxo

```tablegen
// if — lowers para scf.if
def Narval_IfOp : Narval_Op<"if", [
    RecursiveMemoryEffects, NoRegionArguments
]> {
    let arguments = (ins I1:$condition);
    let results = (outs Variadic<AnyType>:$results);
    let regions = (region
        AnyRegion:$then_region,
        AnyRegion:$else_region
    );
    // Lowering: scf.if
}

// for-in — lowers para scf.for ou affine.for
def Narval_ForInOp : Narval_Op<"for_in", [
    RecursiveMemoryEffects
]> {
    let summary = "Narval for-in loop";
    let arguments = (ins
        AnyType:$iterable,
        StrAttr:$loop_var,
        UnitAttr:$is_affine   // true quando range é affine-mappable
    );
    let regions = (region AnyRegion:$body);
    // Lowering: affine.for (quando is_affine=true) ou scf.for
}

// while — lowers para scf.while
def Narval_WhileOp : Narval_Op<"while", [RecursiveMemoryEffects]> {
    let regions = (region
        AnyRegion:$condition_region,
        AnyRegion:$body_region
    );
    // Lowering: scf.while
}

// match — lowers para scf.if chain
def Narval_MatchOp : Narval_Op<"match", [RecursiveMemoryEffects]> {
    let arguments = (ins Narval_ValueType:$subject);
    let results = (outs Variadic<AnyType>:$results);
    let regions = (region VariadicRegion<AnyRegion>:$arms);
    // Cada arm é uma região com um pattern check + body
}
```

#### 6.4 Tensores e Álgebra Linear

```tablegen
// Multiplicação matricial — lowers para linalg.matmul
def Narval_TensorMatmulOp : Narval_Op<"tensor_matmul", [Pure]> {
    let summary = "Matrix multiplication: C = A @ B";
    let arguments = (ins
        AnyTensor:$lhs,      // [M, K]
        AnyTensor:$rhs       // [K, N]
    );
    let results = (outs AnyTensor:$result);  // [M, N]
    let hasVerifier = 1;    // valida shapes em compile-time
    // Lowering: linalg.matmul (com init tensor de zeros)
}

// Adição de tensores — lowers para linalg.add ou vector.add
def Narval_TensorAddOp : Narval_Op<"tensor_add", [Pure, ElementwiseMappable]> {
    let arguments = (ins AnyTensor:$lhs, AnyTensor:$rhs);
    let results = (outs AnyTensor:$result);
    let hasVerifier = 1;
    // Lowering: linalg.add → vector.add → LLVM vector ops (AVX/NEON)
}

// Multiplicação elemento-a-elemento
def Narval_TensorMulOp : Narval_Op<"tensor_mul", [Pure, ElementwiseMappable]> {
    let arguments = (ins AnyTensor:$lhs, AnyTensor:$rhs);
    let results = (outs AnyTensor:$result);
    // Lowering: linalg.mul
}

// map sobre tensor: tensor.map(|x| f(x))
def Narval_TensorMapOp : Narval_Op<"tensor_map", [Pure]> {
    let arguments = (ins AnyTensor:$input);
    let results = (outs AnyTensor:$result);
    let regions = (region AnyRegion:$mapper);   // corpo da closure
    // Lowering: linalg.generic com indexing maps identidade
}

// reduce sobre tensor
def Narval_TensorReduceOp : Narval_Op<"tensor_reduce", [Pure]> {
    let arguments = (ins
        AnyTensor:$input,
        AnyType:$init,
        I64Attr:$dimension     // eixo de redução
    );
    let results = (outs AnyType:$result);
    let regions = (region AnyRegion:$combiner);
    // Lowering: linalg.reduce
}

// transpose
def Narval_TensorTransposeOp : Narval_Op<"tensor_transpose", [Pure]> {
    let arguments = (ins AnyTensor:$input, DenseI64ArrayAttr:$permutation);
    let results = (outs AnyTensor:$result);
    // Lowering: linalg.transpose
}

// fill: cria tensor com valor constante
def Narval_TensorFillOp : Narval_Op<"tensor_fill", [Pure]> {
    let arguments = (ins AnyType:$fill_value, DenseI64ArrayAttr:$shape);
    let results = (outs AnyTensor:$result);
    // Lowering: tensor.empty + linalg.fill
}

// slice/view de tensor
def Narval_TensorSliceOp : Narval_Op<"tensor_slice", [Pure]> {
    let arguments = (ins
        AnyTensor:$source,
        Variadic<Index>:$offsets,
        Variadic<Index>:$sizes,
        Variadic<Index>:$strides
    );
    let results = (outs AnyTensor:$result);
    // Lowering: tensor.extract_slice
}
```

#### 6.5 Error Handling

```tablegen
// Região try/catch — lowering via setjmp/longjmp (preserva comportamento atual)
def Narval_TryRegionOp : Narval_Op<"try_region", [RecursiveMemoryEffects]> {
    let results = (outs Variadic<AnyType>:$results);
    let regions = (region
        AnyRegion:$try_body,
        VariadicRegion<AnyRegion>:$catch_arms,  // um por tipo de exceção
        AnyRegion:$finally_body
    );
    // Lowering: generate_try_stmt.cpp existente (setjmp/longjmp)
}

// Throw
def Narval_ThrowOp : Narval_Op<"throw", [Terminator]> {
    let arguments = (ins Narval_ValueType:$error);
    // Lowering: longjmp para o handler ativo
}

// Wraps return de fallível em Ok(value)
def Narval_ResultWrapOp : Narval_Op<"result_wrap", [Pure]> {
    let arguments = (ins AnyType:$value, BoolAttr:$is_ok);
    let results = (outs Narval_ResultType:$result);
    // Lowering: call nv_make_ok / nv_make_err
}

// propagate — relança se Err, unwrap se Ok
def Narval_PropagateOp : Narval_Op<"propagate"> {
    let arguments = (ins Narval_ResultType:$value);
    let results = (outs AnyType:$unwrapped);
    // Lowering: check tag + conditional longjmp
}
```

#### 6.6 GPU

```tablegen
// Marca uma função como kernel GPU
def Narval_GPUKernelOp : Narval_Op<"gpu_kernel", [
    IsolatedFromAbove, Symbol
]> {
    let arguments = (ins
        SymbolNameAttr:$sym_name,
        StrAttr:$target,          // "cuda", "rocm", "spirv"
        TypeAttrOf<FunctionType>:$function_type
    );
    let regions = (region AnyRegion:$body);
    // Lowering: gpu.func + gpu.module
}

// Launch de kernel GPU
def Narval_GPULaunchOp : Narval_Op<"gpu_launch"> {
    let arguments = (ins
        FlatSymbolRefAttr:$kernel,
        Index:$grid_x, Index:$grid_y, Index:$grid_z,
        Index:$block_x, Index:$block_y, Index:$block_z,
        Variadic<AnyType>:$kernel_operands
    );
    // Lowering: gpu.launch_func
}

// Thread/block index dentro de um kernel
def Narval_GPUThreadIdOp : Narval_Op<"gpu_thread_id", [Pure]> {
    let arguments = (ins StrAttr:$dimension);   // "x", "y", "z"
    let results = (outs Index:$result);
    // Lowering: gpu.thread_id
}
```

#### 6.7 Comptime

```tablegen
// Constante calculada em compile-time — já resolvida pelo ComptimeEvaluator
// Chega ao codegen como um valor literal embutido no IR
def Narval_ComptimeConstOp : Narval_Op<"comptime_const", [Pure, ConstantLike]> {
    let summary = "Compile-time constant (already evaluated by ComptimeEvaluator)";
    let arguments = (ins AnyAttr:$value);
    let results = (outs AnyType:$result);
    // Lowering: arith.constant ou global rodata
    let hasFolder = 1;   // sempre foldável
}

// Chamada comptime já avaliada — resultado é uma constante
// Gerado pelo checker quando inlina o resultado de comptime def
def Narval_ComptimeCallOp : Narval_Op<"comptime_call", [Pure]> {
    let arguments = (ins FlatSymbolRefAttr:$callee, Variadic<AnyType>:$args);
    let results = (outs Variadic<AnyType>:$results);
    // Lowering: fold para narval.comptime_const (o resultado já está resolvido)
    let hasCanonicalizer = 1;
}
```

#### 6.8 Classes e Objetos

```tablegen
// Instancia uma classe (chama __ctor_ClassName)
def Narval_NewOp : Narval_Op<"new"> {
    let summary = "Instantiate a class";
    let arguments = (ins
        StrAttr:$class_name,
        Variadic<Narval_ValueType>:$ctor_args
    );
    let results = (outs Narval_ClassType:$instance);
    // Lowering: nv_alloc_object + __ctor_ClassName(this, args...)
}

// Lê campo de instância
def Narval_GetFieldOp : Narval_Op<"get_field", [Pure]> {
    let arguments = (ins Narval_ClassType:$object, StrAttr:$field_name);
    let results = (outs Narval_ValueType:$result);
    // Lowering: nv_object_get_field(object, field_name)
}

// Escreve campo de instância
def Narval_SetFieldOp : Narval_Op<"set_field"> {
    let arguments = (ins
        Narval_ClassType:$object,
        StrAttr:$field_name,
        Narval_ValueType:$value
    );
    // Lowering: nv_object_set_field(object, field_name, value)
}

// Chamada de método
def Narval_CallMethodOp : Narval_Op<"call_method"> {
    let arguments = (ins
        Narval_ClassType:$receiver,
        StrAttr:$method_name,
        Variadic<Narval_ValueType>:$args
    );
    let results = (outs Narval_ValueType:$result);
    // Lowering: call __method_ClassName_name(this, args...)
}
```

---

## 7. NIRGenerationContext

Substitui `IRGenerationContext` no path NIR. O path LLVM direto continua existindo
(flag `--emit-llvm`) para retrocompatibilidade durante a transição.

### `include/backend/nir/NIRGenerationContext.hpp`

```cpp
#pragma once
#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/OwningOpRef.h>
#include <mlir/Pass/PassManager.h>
#include "NarvalDialect.h"
#include "NarvalOps.h"
#include "frontend/checker/type.hpp"
#include "frontend/ast/ast.hpp"

namespace nv {

class NIRGenerationContext {
public:
    mlir::MLIRContext&          mlir_ctx;
    mlir::OpBuilder             builder;
    mlir::OwningOpRef<mlir::ModuleOp> module;

    // Checker pointer (para resolve_type — mesmo padrão do IRGenerationContext)
    void* type_checker_ptr = nullptr;

    // Tabela de símbolos: nome → mlir::Value (resultado de narval.alloc ou arg)
    using ScopeMap = std::unordered_map<std::string, mlir::Value>;
    std::vector<ScopeMap> scope_stack;

    // Função atual sendo gerada
    mlir::narval::FuncOp current_func;

    // Linker flags extras (mirrors do IRGenerationContext)
    std::vector<std::string> extra_link_items;

    NIRGenerationContext(mlir::MLIRContext& ctx, const std::string& source_file);

    // Gerenciamento de escopo
    void push_scope();
    void pop_scope();
    void define(const std::string& name, mlir::Value val);
    mlir::Value lookup(const std::string& name);

    // Tipo Narval → MLIR Type
    mlir::Type nv_type_to_mlir(std::shared_ptr<nv::Type> nv_type);

    // Lowering e geração do binário final
    // Roda todos os passes na ordem definida em NarvalPassPipeline
    llvm::Expected<std::unique_ptr<llvm::Module>>
    lower_to_llvm_ir(llvm::LLVMContext& llvm_ctx);

    // Debug: dump do NIR em qualquer estágio
    void dump_nir() const;
};

} // namespace nv
```

---

## 8. Pass Manager — Pipeline Completo

### `src/backend/nir/NarvalPassPipeline.cpp`

```cpp
#include <mlir/Pass/PassManager.h>
#include <mlir/Transforms/Passes.h>
#include <mlir/Dialect/Linalg/Passes.h>
#include <mlir/Dialect/Bufferization/Transforms/Passes.h>
#include <mlir/Dialect/Vector/Transforms/Passes.h>
#include <mlir/Conversion/Passes.h>
#include "NarvalPasses.h"

namespace nv {

void build_narval_pass_pipeline(mlir::PassManager& pm, const NarvalPipelineOptions& opts) {

    // ── Nível 1: Narval Dialect → Standard Dialects ──────────────────────────
    pm.addPass(createNarvalCanonicalizationPass());   // fold/simplify ops narval.*
    pm.addPass(createLowerNarvalControlFlowPass());   // narval.if/for/while → scf.*
    pm.addPass(createLowerNarvalFunctionsPass());     // narval.func → func.func
    pm.addPass(createLowerNarvalClassesPass());       // narval.new/get_field → runtime calls
    pm.addPass(createLowerNarvalErrorHandlingPass()); // narval.try_region/throw → setjmp/longjmp

    // ── Nível 2: Tensor Pipeline ──────────────────────────────────────────────
    if (opts.enable_tensor_opts) {
        pm.addPass(createLowerTensorToLinalgPass());  // narval.tensor_* → linalg.*
        pm.addNestedPass<mlir::func::FuncOp>(
            mlir::createLinalgFusionOfTensorOpsPass()
        );
        pm.addNestedPass<mlir::func::FuncOp>(
            mlir::createLinalgTilingPass({opts.tile_m, opts.tile_n, opts.tile_k})
        );
    }

    // ── Nível 3: Ownership → MemRef ───────────────────────────────────────────
    pm.addPass(createLowerOwnershipToMemRefPass());   // narval.borrow/move → memref.*
    pm.addPass(mlir::bufferization::createOneShotBufferizePass());
    pm.addPass(mlir::bufferization::createBufferDeallocationPass());

    // ── Nível 4: Afine e SCF ─────────────────────────────────────────────────
    pm.addNestedPass<mlir::func::FuncOp>(mlir::createConvertLinalgToLoopsPass());
    pm.addNestedPass<mlir::func::FuncOp>(mlir::affine::createLoopFusionPass());
    pm.addNestedPass<mlir::func::FuncOp>(mlir::affine::createAffineLoopNormalizePass());

    // ── Nível 5: GPU (opcional) ───────────────────────────────────────────────
    if (opts.target_gpu != GPUTarget::None) {
        pm.addPass(createLowerGPUKernelsPass(opts.target_gpu));
        // gpu.func → nvvm.func / rocdl.func / spirv.func
        if (opts.target_gpu == GPUTarget::CUDA)
            pm.addPass(mlir::createConvertGpuOpsToNVVMOpsPass());
        else if (opts.target_gpu == GPUTarget::ROCM)
            pm.addPass(mlir::createConvertGpuOpsToROCDLOpsPass());
    }

    // ── Nível 6: Vectorização SIMD ────────────────────────────────────────────
    if (opts.enable_simd) {
        pm.addNestedPass<mlir::func::FuncOp>(mlir::createConvertLinalgToVectorPass());
        pm.addNestedPass<mlir::func::FuncOp>(mlir::vector::createVectorTransferFullPartialRewritePass());
        pm.addNestedPass<mlir::func::FuncOp>(mlir::vector::createLowerVectorMasksPass());
    }

    // ── Nível 7: Lowering Final para LLVM Dialect ─────────────────────────────
    pm.addPass(mlir::createConvertSCFToCFPass());
    pm.addPass(mlir::createConvertVectorToLLVMPass());
    pm.addPass(mlir::createConvertMemRefToLLVMPass());
    pm.addPass(mlir::createArithToLLVMConversionPass());
    pm.addPass(mlir::createConvertFuncToLLVMPass());
    pm.addPass(mlir::createReconcileUnrealizedCastsPass());
}

} // namespace nv
```

---

## 9. Passes de Narval — Especificação

### 9.1 `NarvalCanonicalizationPass`

Arquivo: `src/backend/nir/passes/NarvalCanonicalization.cpp`

Patterns de simplificação registrados no dialect:

```cpp
// narval.tensor_add(x, 0) → x
struct FoldTensorAddZero : OpRewritePattern<narval::TensorAddOp> {
    LogicalResult matchAndRewrite(narval::TensorAddOp op,
                                  PatternRewriter& rewriter) const override {
        if (auto cst = op.getRhs().getDefiningOp<narval::ComptimeConstOp>()) {
            if (isZero(cst.getValue())) {
                rewriter.replaceOp(op, op.getLhs());
                return success();
            }
        }
        return failure();
    }
};

// narval.tensor_mul(x, 1) → x
struct FoldTensorMulOne : OpRewritePattern<narval::TensorMulOp> { ... };

// narval.tensor_transpose(narval.tensor_transpose(x, p), inv(p)) → x
struct FoldDoubleTranspose : OpRewritePattern<narval::TensorTransposeOp> { ... };

// narval.move(narval.borrow(x)) → x  (move de uma referência cria cópia)
struct SimplifyMoveOfBorrow : OpRewritePattern<narval::MoveOp> { ... };

// narval.comptime_call → narval.comptime_const (inlina resultado já conhecido)
struct FoldComptimeCall : OpRewritePattern<narval::ComptimeCallOp> { ... };
```

Todos registrados em `NarvalOps.cpp::getCanonicalizationPatterns()`.

### 9.2 `LowerNarvalControlFlowPass`

Arquivo: `src/backend/nir/passes/LowerNarvalToStandard.cpp`

```cpp
// narval.if → scf.if
struct LowerNarvalIf : OpConversionPattern<narval::IfOp> {
    LogicalResult matchAndRewrite(narval::IfOp op,
                                  OpAdaptor adaptor,
                                  ConversionPatternRewriter& rewriter) const override {
        auto scf_if = rewriter.create<scf::IfOp>(op.getLoc(),
            op.getResultTypes(), adaptor.getCondition(),
            /*withElseRegion=*/!op.getElseRegion().empty()
        );
        rewriter.inlineRegionBefore(op.getThenRegion(), scf_if.getThenRegion(), ...);
        rewriter.inlineRegionBefore(op.getElseRegion(), scf_if.getElseRegion(), ...);
        rewriter.replaceOp(op, scf_if.getResults());
        return success();
    }
};

// narval.for_in (is_affine=true, range estático) → affine.for
struct LowerNarvalForToAffine : OpConversionPattern<narval::ForInOp> {
    LogicalResult matchAndRewrite(narval::ForInOp op, ...) const override {
        if (!op.getIsAffine()) return failure();
        // Extrai lb/ub/step do RangeExpr embutido nos operandos
        auto affine_for = rewriter.create<affine::AffineForOp>(op.getLoc(), lb, ub, step);
        rewriter.inlineRegionBefore(op.getBody(), affine_for.getBody(), ...);
        rewriter.replaceOp(op, {});
        return success();
    }
};

// narval.for_in (is_affine=false) → scf.for
struct LowerNarvalForToSCF : OpConversionPattern<narval::ForInOp> { ... };

// narval.while → scf.while
struct LowerNarvalWhile : OpConversionPattern<narval::WhileOp> { ... };
```

### 9.3 `LowerTensorToLinalgPass`

Arquivo: `src/backend/nir/passes/LowerTensorToLinalg.cpp`

```cpp
// narval.tensor_matmul → linalg.matmul
struct LowerMatmul : OpRewritePattern<narval::TensorMatmulOp> {
    LogicalResult matchAndRewrite(narval::TensorMatmulOp op,
                                  PatternRewriter& rewriter) const override {
        auto loc = op.getLoc();
        // Criar tensor de output zerado
        auto out_type = op.getResult().getType().cast<RankedTensorType>();
        auto empty = rewriter.create<tensor::EmptyOp>(loc, out_type.getShape(),
                                                       out_type.getElementType());
        auto zero = rewriter.create<arith::ConstantOp>(loc,
            rewriter.getZeroAttr(out_type.getElementType()));
        auto filled = rewriter.create<linalg::FillOp>(loc, zero, empty).getResult(0);

        auto matmul = rewriter.create<linalg::MatmulOp>(loc,
            TypeRange{out_type},
            ValueRange{op.getLhs(), op.getRhs()},
            ValueRange{filled}
        );
        rewriter.replaceOp(op, matmul.getResult(0));
        return success();
    }
};

// narval.tensor_add → linalg.add
struct LowerTensorAdd : OpRewritePattern<narval::TensorAddOp> { ... };

// narval.tensor_map → linalg.generic
struct LowerTensorMap : OpRewritePattern<narval::TensorMapOp> {
    // Constrói indexing maps identidade para todos os dims
    // Inline o corpo da closure na região do linalg.generic
    ...
};
```

### 9.4 `LowerOwnershipToMemRefPass`

Arquivo: `src/backend/nir/passes/LowerOwnershipToMemRef.cpp`

Este pass é o coração do sistema de ownership implícito da Narval.

**Análise:**

1. Constrói grafo de uso/definição de todos `narval.alloc`, `narval.move`, `narval.borrow`, `narval.borrow_mut`
2. Detecta quando um valor pode ser reutilizado in-place (ownership analysis)
3. Decide quais operações de tensor podem ser bufferizadas in-place vs. precisam cópia

**Lowering:**

```cpp
// narval.borrow(x) → memref.view ou getelementptr
struct LowerBorrow : OpConversionPattern<narval::BorrowOp> {
    LogicalResult matchAndRewrite(...) const override {
        // Se x é memref: criar view sem cópia
        // Se x é tensor: criar slice view
        ...
    }
};

// narval.move(x) → transferência semântica
// Em runtime: identity (mesmo ponteiro)
// O pass insere narval.drop no ponto de last-use do source
struct LowerMove : OpConversionPattern<narval::MoveOp> {
    LogicalResult matchAndRewrite(...) const override {
        rewriter.replaceOp(op, adaptor.getSource());
        // Inserir narval.drop no dominance frontier do source
        insertDropAtLastUse(op.getSource(), rewriter);
        return success();
    }
};

// narval.drop → call nv_free
struct LowerDrop : OpConversionPattern<narval::DropOp> {
    LogicalResult matchAndRewrite(...) const override {
        rewriter.create<func::CallOp>(op.getLoc(), "nv_free",
                                      TypeRange{}, ValueRange{adaptor.getValue()});
        rewriter.eraseOp(op);
        return success();
    }
};
```

### 9.5 `LowerGPUKernelsPass`

Arquivo: `src/backend/nir/passes/LowerGPUKernels.cpp`

```cpp
// narval.gpu_kernel → gpu.func dentro de gpu.module
struct LowerGPUKernel : OpConversionPattern<narval::GPUKernelOp> {
    LogicalResult matchAndRewrite(narval::GPUKernelOp op, ...) const override {
        // 1. Criar gpu.module
        auto gpu_module = rewriter.create<gpu::GPUModuleOp>(op.getLoc(),
                                                             op.getSymName());
        // 2. Criar gpu.func dentro do módulo
        auto gpu_func = rewriter.create<gpu::GPUFuncOp>(op.getLoc(),
            op.getSymName(), op.getFunctionType());
        gpu_func->setAttr(gpu::GPUDialect::getKernelFuncAttrName(),
                          rewriter.getUnitAttr());
        // 3. Inline o corpo
        rewriter.inlineRegionBefore(op.getBody(), gpu_func.getBody(), ...);
        rewriter.eraseOp(op);
        return success();
    }
};

// narval.gpu_launch → gpu.launch_func
struct LowerGPULaunch : OpConversionPattern<narval::GPULaunchOp> { ... };

// narval.gpu_thread_id → gpu.thread_id
struct LowerGPUThreadId : OpConversionPattern<narval::GPUThreadIdOp> {
    LogicalResult matchAndRewrite(...) const override {
        auto dim = op.getDimension() == "x" ? gpu::Dimension::x :
                   op.getDimension() == "y" ? gpu::Dimension::y : gpu::Dimension::z;
        rewriter.replaceOpWithNewOp<gpu::ThreadIdOp>(op, dim);
        return success();
    }
};
```

---

## 10. Narval Dialect Verifiers

Verificação de shapes em compile-time (em `NarvalOps.cpp`):

```cpp
LogicalResult narval::TensorMatmulOp::verify() {
    auto lhs = getLhs().getType().cast<RankedTensorType>();
    auto rhs = getRhs().getType().cast<RankedTensorType>();
    auto res = getResult().getType().cast<RankedTensorType>();

    if (lhs.getRank() != 2 || rhs.getRank() != 2)
        return emitOpError("matmul requires rank-2 tensors");

    int64_t M = lhs.getDimSize(0), K1 = lhs.getDimSize(1);
    int64_t K2 = rhs.getDimSize(0), N  = rhs.getDimSize(1);

    if (K1 != ShapedType::kDynamic && K2 != ShapedType::kDynamic && K1 != K2)
        return emitOpError() << "inner dimensions mismatch: " << K1 << " vs " << K2;

    if (res.getDimSize(0) != M || res.getDimSize(1) != N)
        return emitOpError("result shape does not match [M, N]");

    return success();
}
```

Erros emitidos pelo verifier têm localização MLIR (linha/coluna do `.nv` original) via
`Location` preservado durante a emissão do NIR.

---

## 11. Transform Dialect — Otimizações Controláveis

Sintaxe Narval para anotações de otimização:

```narval
@optimize {
    tile(16, 16, 4)
    vectorize
    unroll(4)
    parallelize(axis: 0)
}
def matmul(a: Tensor<float, [512, 512]>, b: Tensor<float, [512, 512]>): Tensor<float, [512, 512]> {
    return a @ b
}
```

O atributo `@optimize { ... }` é processado pelo checker como um `@[optimize(...)]`
e gera Transform Dialect IR separado que referencia a função-alvo pelo symbol name.

**IR Gerado (Transform Dialect):**

```mlir
// Transform script gerado para matmul
transform.sequence failures(propagate) {
^bb0(%root: !transform.any_op):
    %matmul = transform.structured.match ops{["linalg.matmul"]}
              in %root : (!transform.any_op) -> !transform.any_op

    %tiled, %loops:3 = transform.structured.tile_using_forall %matmul
        tile_sizes [16, 16, 4]
        : (!transform.any_op) -> (!transform.any_op, !transform.any_op,
                                   !transform.any_op, !transform.any_op)

    transform.structured.vectorize %tiled : !transform.any_op

    %func = transform.get_parent_op %tiled {isolated_from_above}
        : (!transform.any_op) -> !transform.any_op
    transform.apply_patterns.canonicalization to %func : !transform.any_op
    transform.apply_patterns.vector.lower_contraction to %func
        lowering_strategy = "outerproduct" : !transform.any_op
}
```

**Anotações disponíveis:**

| Anotação | Transform Op gerado |
|---|---|
| `tile(M, N, K)` | `transform.structured.tile_using_forall` |
| `vectorize` | `transform.structured.vectorize` |
| `unroll(N)` | `transform.loop.unroll` |
| `parallelize(axis: K)` | `transform.structured.tile_using_forall` + `gpu.map_forall_to_blocks` |
| `fuse(with: other)` | `transform.structured.fuse_into_containing_op` |
| `interchange(order: [1,0,2])` | `transform.structured.interchange_loops` |
| `cache(level: 2, size: 256k)` | `transform.structured.pack` + `transform.structured.unpack` |

---

## 12. Emissão NIR pelo Codegen

### Como o codegen muda

O `generate_ir.cpp` atual instancia `IRGenerationContext` com `llvm::IRBuilder`.
Com NIR, instancia `NIRGenerationContext` com `mlir::OpBuilder`, e as visitas de nó
emitem ops MLIR em vez de LLVM IR direto.

**Exemplo — geração de função:**

Atual (`generate_function_stmt.cpp`):
```cpp
llvm::Function* fn = llvm::Function::Create(ft, linkage, name, module);
```

NIR (`generate_function_stmt.cpp` refatorado):
```cpp
auto fn = builder.create<mlir::narval::FuncOp>(loc,
    node->name, fn_type,
    node->abi, node->is_fallible, node->is_async, node->is_comptime
);
```

**Exemplo — operação binária com tensores:**

```cpp
// Em generate_binary_expr.cpp:
if (lhs_type->kind == Kind::TENSOR && op == "*") {
    auto result = nir_ctx.builder.create<mlir::narval::TensorMatmulOp>(
        loc, result_mlir_type, lhs_val, rhs_val
    );
    return result;
}
```

### Estratégia de migração

O build tem uma flag `NARVAL_USE_NIR` (default: OFF durante transição):

```cmake
option(NARVAL_USE_NIR "Use MLIR NIR pipeline instead of direct LLVM codegen" OFF)
```

Com `OFF`: roda `IRGenerationContext` (LLVM IR direto) — caminho atual, sem alterações.  
Com `ON`: roda `NIRGenerationContext` + pass pipeline → LLVM IR via MLIR.

Os dois caminhos compartilham:
- Todo o frontend (Lexer, Parser, Checker, ComptimeEvaluator)
- Runtime C (`src/backend/runtime/`)
- A saída final (LLVM IR / binário)

---

## 13. Location Tracking

Toda op MLIR criada recebe a localização do nó AST original:

```cpp
mlir::Location loc_from_ast(mlir::MLIRContext& ctx, const PositionData* pos) {
    if (!pos) return mlir::UnknownLoc::get(&ctx);
    return mlir::FileLineColLoc::get(&ctx,
        llvm::StringRef(pos->filename),
        static_cast<unsigned>(pos->line),
        static_cast<unsigned>(pos->col[0] + 1)
    );
}
```

Erros emitidos por passes (shape mismatch, tipo incompatível) automaticamente incluem
a linha do arquivo `.nv` original:

```
error: inner dimensions mismatch: 128 vs 64
  → main.nv:42:13
  |
42 |   c = a @ b
  |         ^
```

---

## 14. `NarvalPasses.td`

```tablegen
include "mlir/Pass/PassBase.td"

def NarvalCanonicalization : Pass<"narval-canonicalize", "mlir::ModuleOp"> {
    let summary = "Fold and simplify narval.* operations";
    let constructor = "nv::createNarvalCanonicalizationPass()";
}

def LowerNarvalControlFlow : Pass<"lower-narval-cf", "mlir::ModuleOp"> {
    let summary = "Lower narval control flow to scf/affine dialects";
    let dependentDialects = ["mlir::scf::SCFDialect", "mlir::affine::AffineDialect"];
    let constructor = "nv::createLowerNarvalControlFlowPass()";
}

def LowerNarvalFunctions : Pass<"lower-narval-funcs", "mlir::ModuleOp"> {
    let summary = "Lower narval.func to func.func";
    let dependentDialects = ["mlir::func::FuncDialect"];
    let constructor = "nv::createLowerNarvalFunctionsPass()";
}

def LowerTensorToLinalg : Pass<"lower-narval-tensor", "mlir::func::FuncOp"> {
    let summary = "Lower narval.tensor_* ops to linalg dialect";
    let dependentDialects = [
        "mlir::linalg::LinalgDialect",
        "mlir::tensor::TensorDialect",
        "mlir::arith::ArithDialect"
    ];
    let constructor = "nv::createLowerTensorToLinalgPass()";
}

def LowerOwnershipToMemRef : Pass<"lower-narval-ownership", "mlir::func::FuncOp"> {
    let summary = "Lower narval ownership ops to memref";
    let dependentDialects = ["mlir::memref::MemRefDialect"];
    let constructor = "nv::createLowerOwnershipToMemRefPass()";
}

def LowerGPUKernels : Pass<"lower-narval-gpu", "mlir::ModuleOp"> {
    let summary = "Lower narval.gpu_kernel/launch to gpu dialect";
    let dependentDialects = ["mlir::gpu::GPUDialect"];
    let constructor = "nv::createLowerGPUKernelsPass()";
    let options = [
        Option<"target", "target", "std::string", "\"cuda\"",
               "GPU target: cuda, rocm, spirv">
    ];
}
```

---

## 15. Roadmap de Implementação

### Fase 1 — Fundação do Dialeto (bloqueante)
1. `CMakeLists.txt`: adicionar deps MLIR, setup TableGen
2. `NarvalDialect.td` + `NarvalDialect.h/cpp` — registro do dialect
3. `NarvalTypes.td` — `!narval.value`, `!narval.class`, `!narval.option`, `!narval.result`
4. `NarvalOps.td` — apenas `narval.func`, `narval.call`, `narval.return` (subconjunto mínimo)
5. `NIRGenerationContext` básico com `mlir::OpBuilder`
6. `LowerNarvalFunctionsPass` — narval.func → func.func
7. `LowerToLLVM` — path completo funcional (sem otimizações)
8. Flag `NARVAL_USE_NIR` no CMake — compilador roda os dois paths e compara output

### Fase 2 — Controle de Fluxo
9. Ops: `narval.if`, `narval.for_in`, `narval.while`, `narval.match`
10. `LowerNarvalControlFlowPass` — scf/affine
11. Refatorar `generate_if_stmt.cpp`, `generate_for_stmt.cpp` etc. para emitir NIR

### Fase 3 — Ownership
12. Ops: `narval.alloc`, `narval.move`, `narval.borrow`, `narval.borrow_mut`, `narval.drop`
13. `LowerOwnershipToMemRefPass` — análise de lifetime + inserção de drops
14. Integração com bufferization pipeline

### Fase 4 — Tensores e Álgebra Linear
15. `!narval.tensor` type + `NarvalTensorType` no type system do checker (ver COMPTIME_SPEC)
16. Ops: `narval.tensor_matmul`, `narval.tensor_add`, `narval.tensor_map`, `narval.tensor_reduce`
17. Shape verifiers compile-time nos ops de tensor
18. `LowerTensorToLinalgPass`
19. Tiling e fusion via `mlir::createLinalgTilingPass`

### Fase 5 — SIMD
20. `@vectorize` attribute → loop vectorization metadata
21. `LowerLinalgToVectorPass`
22. `LowerVectorToLLVMPass` — AVX2/AVX-512/NEON via LLVM target features
23. `comptime if SIMD == "avx2"` via `target.simd()` built-in

### Fase 6 — Canonicalização e Pattern Rewriting
24. `NarvalCanonicalizationPass` com os patterns da Seção 9.1
25. `NarvalDialect::getCanonicalizationPatterns()` para fold automático
26. CSE + DCE passes sobre NIR

### Fase 7 — Transform Dialect
27. Parsing de `@optimize { }` no atributo checker
28. Geração de Transform Dialect IR por função anotada
29. Aplicação do transform script no `PassManager`

### Fase 8 — GPU
30. Ops: `narval.gpu_kernel`, `narval.gpu_launch`, `narval.gpu_thread_id`
31. `LowerGPUKernelsPass` → gpu.func + gpu.launch_func
32. CUDA path: `ConvertGpuOpsToNVVMOpsPass` + PTX emission
33. Wrapper de launch gerado automaticamente

---

## 16. Limitações Conhecidas

- A bufferization one-shot do MLIR assume semântica funcional (tensors imutáveis);
  objetos Narval com aliasing via `self` precisam de análise extra antes de bufferizar
- O Transform Dialect ainda não suporta transformações sobre `narval.*` ops diretamente;
  precisa esperar o lowering para `linalg.*` primeiro (por isso o pipeline aplica os
  transforms após `LowerTensorToLinalgPass`)
- GPU path requer MLIR compilado com `MLIR_ENABLE_CUDA_RUNNER=ON` para testes em runtime
- Ownership analysis (Fase 3) começa conservador (insere drops cedo); análise mais fina
  (escape analysis, alias analysis) é trabalho futuro
- Classes Narval (map-backed via NVMap) não são bufferizáveis — continuam como `narval.value`
  opaco que o runtime gerencia; apenas `Tensor` e primitivos low-level entram no pipeline
  de bufferização
