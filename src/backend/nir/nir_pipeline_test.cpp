#include "backend/nir/NIRGenerationContext.hpp"
#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalTypes.h"
#include "backend/nir/NarvalPasses.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Bufferization/IR/Bufferization.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"

// nv_runtime.h only opens its extern "C" block *after* including
// prototypes.h, so wrap the whole include for C++ linkage.
extern "C" {
#include "backend/runtime/nv_runtime.h"
}

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static int _early = (std::puts("nir_test: loading"), std::fflush(stdout), 0);

static void fail(const char* test, const char* msg) {
    std::fprintf(stderr, "[FAIL] %s: %s\n", test, msg);
    std::exit(1);
}

static void ok(const char* test) {
    std::printf("[PASS] %s\n", test);
    std::fflush(stdout);
}

//===----------------------------------------------------------------------===//
// Build a minimal NIR module using ONLY standard func/arith ops (no narval ops).
// This tests the infrastructure (PassManager, LLVM translation) with minimal risk.
//===----------------------------------------------------------------------===//

static void test_infrastructure() {
    const char* NAME = "test_infrastructure";

    mlir::MLIRContext ctx(mlir::MLIRContext::Threading::DISABLED);
    nv::NIRGenerationContext nir(ctx, "test.nv");
    auto& b   = nir.get_builder();
    auto  loc = b.getUnknownLoc();

    auto i64 = b.getI64Type();

    // Build: func @add(i64, i64) -> i64 { return %a + %b }
    // func::FuncOp::create(b, loc, ...) already inserts into the module
    // at the builder's insertion point — no extra push_back needed.
    auto fn_type = mlir::FunctionType::get(&ctx, {i64, i64}, {i64});
    auto fn = mlir::func::FuncOp::create(b, loc, "ir_add", fn_type);
    fn.setPublic();

    auto* entry = fn.addEntryBlock();
    b.setInsertionPointToStart(entry);

    auto sum = mlir::arith::AddIOp::create(b, loc,
                   fn.getArgument(0), fn.getArgument(1));
    mlir::func::ReturnOp::create(b, loc, mlir::ValueRange{sum});

    // Lower to LLVM IR.
    llvm::LLVMContext llvm_ctx;
    auto llvm_mod = nir.lower_to_llvm_ir(llvm_ctx);
    if (!llvm_mod)
        fail(NAME, llvm::toString(llvm_mod.takeError()).c_str());

    // Verify the LLVM module has the expected function.
    if (!(*llvm_mod)->getFunction("ir_add"))
        fail(NAME, "ir_add not found in LLVM module");

    ok(NAME);
}

//===----------------------------------------------------------------------===//
// Test narval.alloc + narval.drop (ownership ops).
// Builds: module with 1 func that allocates and drops a value.
//===----------------------------------------------------------------------===//

static void test_alloc_drop() {
    const char* NAME = "test_alloc_drop";

    mlir::MLIRContext ctx(mlir::MLIRContext::Threading::DISABLED);
    nv::NIRGenerationContext nir(ctx, "test.nv");
    auto& b   = nir.get_builder();
    auto  loc = b.getUnknownLoc();

    auto ptr = mlir::LLVM::LLVMPointerType::get(&ctx);

    // Declare runtime: nv_alloc_int() -> ptr, nv_free(ptr) -> void
    nir.ensure_runtime_func("nv_alloc_int",
        mlir::FunctionType::get(&ctx, {}, {ptr}));
    nir.ensure_runtime_func("nv_free",
        mlir::FunctionType::get(&ctx, {ptr}, {}));

    // Build: func @alloc_test() -> void
    auto fn_type = mlir::FunctionType::get(&ctx, {}, {});
    auto fn = mlir::func::FuncOp::create(b, loc, "alloc_test", fn_type);
    fn.setPublic();
    // No push_back needed — create() already inserts at builder's position.

    auto* entry = fn.addEntryBlock();
    b.setInsertionPointToStart(entry);

    // narval.alloc "nv_alloc_int", 1 : !narval.value
    auto val_type = mlir::narval::ValueType::get(&ctx);
    mlir::narval::AllocOp::create(b, loc, val_type,
        b.getStringAttr("nv_alloc_int"),
        b.getI32IntegerAttr(1));

    // For the test: just return (ownership pass will add the drop).
    mlir::func::ReturnOp::create(b, loc, mlir::ValueRange{});

    // Lower.
    llvm::LLVMContext llvm_ctx;
    auto llvm_mod = nir.lower_to_llvm_ir(llvm_ctx);
    if (!llvm_mod)
        fail(NAME, llvm::toString(llvm_mod.takeError()).c_str());

    if (!(*llvm_mod)->getFunction("alloc_test"))
        fail(NAME, "alloc_test not found in LLVM module");

    ok(NAME);
}

//===----------------------------------------------------------------------===//
// narval.tensor_to_value — phase A lowering test.
//
// Builds func @box_tensor_test() that:
//   1. creates a tensor<2x3xf32> (arith.constant dense producer)
//   2. boxes it with narval.tensor_to_value
//   3. releases it with narval.drop
// Then runs the phase A pipeline (LowerNarvalToStandardPass is where
// TensorToValueOp is converted). Success means:
//   - no narval.tensor_to_value remains
//   - a func.call @nv_box_tensor with the runtime ABI signature
//     (11 x i64 operands) was emitted
//   - the hand-off ops for phase B are present: memref.alloc +
//     bufferization.materialize_in_destination +
//     memref.extract_aligned_pointer_as_index
// (Full phase B bufferization of tensor chains is a separate, pre-existing
// pipeline gap — see skill narval-lang — and is not exercised here.)
//===----------------------------------------------------------------------===//

static void test_tensor_to_value_pipeline() {
    const char* NAME = "test_tensor_to_value_pipeline";

    mlir::MLIRContext ctx(mlir::MLIRContext::Threading::DISABLED);
    nv::NIRGenerationContext nir(ctx, "test.nv");
    auto& b   = nir.get_builder();
    auto  loc = b.getUnknownLoc();

    // Dialects this test emits directly (tensor/linalg) and that the new
    // lowering produces (bufferization dialect).
    ctx.loadDialect<mlir::tensor::TensorDialect>();
    ctx.loadDialect<mlir::linalg::LinalgDialect>();
    ctx.loadDialect<mlir::bufferization::BufferizationDialect>();

    // func @box_tensor_test() — void signature, like production main.start.
    auto fn_type = mlir::FunctionType::get(&ctx, {}, {});
    auto fn = mlir::func::FuncOp::create(b, loc, "box_tensor_test", fn_type);
    fn.setPublic();

    auto* entry = fn.addEntryBlock();
    b.setInsertionPointToStart(entry);

    // %cst = arith.constant dense<[...]> : tensor<2x3xf32>
    // (constant tensor producer — NarvalLinalgVectorizePass rewrites any
    //  linalg op in phase B, so avoid linalg.fill as the source here)
    auto f32   = b.getF32Type();
    llvm::SmallVector<int64_t> shape = {2, 3};
    auto tensor_ty = mlir::RankedTensorType::get(shape, f32);
    float dense_vals[6] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    auto cst = mlir::arith::ConstantOp::create(b, loc,
        mlir::DenseElementsAttr::get(tensor_ty,
                                     llvm::ArrayRef<float>(dense_vals, 6)));

    // %v = narval.tensor_to_value %cst : (tensor<2x3xf32>) -> !narval.value
    auto t2v = mlir::narval::TensorToValueOp::create(b, loc,
        cst.getOperation()->getResult(0));
    mlir::Value boxed = t2v.getOperation()->getResult(0);

    // narval.drop %v — consume the boxed value (like the ownership pass does)
    mlir::narval::DropOp::create(b, loc, boxed);

    // return
    mlir::func::ReturnOp::create(b, loc, mlir::ValueRange{});

    // Run the phase A pipeline (contains LowerNarvalToStandardPass).
    mlir::PassManager pm(&ctx);
    nv::build_narval_pass_pipeline_phase_a(pm, nir.get_module());
    if (mlir::failed(pm.run(nir.get_module())))
        fail(NAME, "phase A pipeline failed");

    // Structural assertions on the lowered module.
    bool foundCall = false, foundMaterialize = false;
    bool foundAlloc = false, foundPtrExtract = false, foundT2V = false;
    nir.get_module().walk([&](mlir::Operation* op) {
        if (mlir::isa<mlir::narval::TensorToValueOp>(op)) {
            foundT2V = true;
            return;
        }
        if (mlir::isa<mlir::func::CallOp>(op)) {
            auto call = mlir::cast<mlir::func::CallOp>(op);
            if (call.getCallee() == "nv_box_tensor") {
                foundCall = true;
                if (call.getNumOperands() != 12) {
                    std::fprintf(stderr,
                        "[FAIL] %s: nv_box_tensor call must have 12 operands, got %lu\n",
                        NAME, (unsigned long)call.getNumOperands());
                    std::exit(1);
                }
            }
            return;
        }
        if (mlir::isa<mlir::bufferization::MaterializeInDestinationOp>(op)) {
            foundMaterialize = true;
            return;
        }
        if (mlir::isa<mlir::memref::AllocOp>(op)) {
            foundAlloc = true;
            return;
        }
        if (mlir::isa<mlir::memref::ExtractAlignedPointerAsIndexOp>(op)) {
            foundPtrExtract = true;
            return;
        }
    });

    if (foundT2V)
        fail(NAME, "narval.tensor_to_value was not lowered away");
    if (!foundCall)
        fail(NAME, "nv_box_tensor call not found after lowering");
    if (!foundMaterialize)
        fail(NAME, "bufferization.materialize_in_destination not found");
    if (!foundAlloc)
        fail(NAME, "memref.alloc not found");
    if (!foundPtrExtract)
        fail(NAME, "memref.extract_aligned_pointer_as_index not found");

    ok(NAME);
}

//===----------------------------------------------------------------------===//
// nv_box_tensor runtime unit tests.
// The function reads a flat data buffer (as passed by the NIR lowering) and
// produces a boxed map Value { __data__: NVArray of elements, __shape__:
// NVArray of dimension sizes }. Each case validates type detection and data
// ordering for a different element size / dtype.
//===----------------------------------------------------------------------===//

static int is_int_obj(NvObject* o, int32_t expect) {
    return o && o->ob_type == NVInt_Type && ((NVInt*)o)->value == expect;
}

static int is_float_obj(NvObject* o, double expect) {
    return o && o->ob_type == NVFloat_Type && ((NVFloat*)o)->value == expect;
}

// Reads a named field of a map Value and requires it to be an NVArray.
static NVArray* box_field_array(Value* self, const char* key,
                                const char* test_name) {
    Value f = {NULL};
    nv_object_get_field(&f, self, key);
    if (!f.obj || f.obj->ob_type != NVArray_Type) {
        std::fprintf(stderr, "[FAIL] %s: field '%s' missing or not an array\n",
                     test_name, key);
        std::exit(1);
    }
    return (NVArray*)f.obj;
}

static void test_nv_box_tensor_runtime() {
    const char* NAME = "test_nv_box_tensor_runtime";

    // Mirrors main.start: initialize the runtime type system before use.
    register_global_init();

    // 1) f32 buffer (elem_size 4, fractional values) → NVFloat elements.
    {
        float buf[6] = {1.5f, -2.25f, 3.75f, 0.5f, 10.0f, -1.0f};
        Value v = nv_box_tensor((int64_t)(intptr_t)buf, 2, 4, 2,
                                2, 3, 0, 0, 0, 0, 0, 0);
        if (!v.obj || v.obj->ob_type != NVMap_Type)
            fail(NAME, "f32 case: expected a map value");

        NVArray* data  = box_field_array(&v, "__data__", NAME);
        NVArray* shape = box_field_array(&v, "__shape__", NAME);
        if (data->size != 6) fail(NAME, "f32 case: __data__ size != 6");
        if (shape->size != 2) fail(NAME, "f32 case: __shape__ size != 2");
        if (!is_int_obj(shape->elements[0].obj, 2) ||
            !is_int_obj(shape->elements[1].obj, 3))
            fail(NAME, "f32 case: wrong shape dims");

        const double exp[6] = {1.5, -2.25, 3.75, 0.5, 10.0, -1.0};
        for (int i = 0; i < 6; i++)
            if (!is_float_obj(data->elements[i].obj, exp[i]))
                fail(NAME, "f32 case: element value mismatch");
    }

    // 2) i32 buffer (elem_size 4, int-like values) → NVInt elements.
    {
        int32_t buf[6] = {10, 20, 30, 40, 50, 60};
        Value v = nv_box_tensor((int64_t)(intptr_t)buf, 2, 4, 1,
                                2, 3, 0, 0, 0, 0, 0, 0);
        if (!v.obj || v.obj->ob_type != NVMap_Type)
            fail(NAME, "i32 case: expected a map value");

        NVArray* data = box_field_array(&v, "__data__", NAME);
        if (data->size != 6) fail(NAME, "i32 case: __data__ size != 6");
        const int32_t exp[6] = {10, 20, 30, 40, 50, 60};
        for (int i = 0; i < 6; i++)
            if (!is_int_obj(data->elements[i].obj, exp[i]))
                fail(NAME, "i32 case: element value mismatch");
    }

    // 3) f64 buffer (elem_size 8) → NVFloat elements (always float path).
    {
        double buf[4] = {0.25, 1.5, 2.75, -4.5};
        Value v = nv_box_tensor((int64_t)(intptr_t)buf, 1, 8, 2,
                                4, 0, 0, 0, 0, 0, 0, 0);
        if (!v.obj || v.obj->ob_type != NVMap_Type)
            fail(NAME, "f64 case: expected a map value");

        NVArray* data = box_field_array(&v, "__data__", NAME);
        if (data->size != 4) fail(NAME, "f64 case: __data__ size != 4");
        const double exp[4] = {0.25, 1.5, 2.75, -4.5};
        for (int i = 0; i < 4; i++)
            if (!is_float_obj(data->elements[i].obj, exp[i]))
                fail(NAME, "f64 case: element value mismatch");
    }

    // 4) i16 buffer (elem_size 2) → NVInt elements.
    {
        int16_t buf[3] = {100, -200, 300};
        Value v = nv_box_tensor((int64_t)(intptr_t)buf, 1, 2, 1,
                                3, 0, 0, 0, 0, 0, 0, 0);
        if (!v.obj) fail(NAME, "i16 case: expected a map value");
        NVArray* data = box_field_array(&v, "__data__", NAME);
        if (data->size != 3) fail(NAME, "i16 case: __data__ size != 3");
        const int32_t exp[3] = {100, -200, 300};
        for (int i = 0; i < 3; i++)
            if (!is_int_obj(data->elements[i].obj, exp[i]))
                fail(NAME, "i16 case: element value mismatch");
    }

    // 5) i8 buffer (elem_size 1) → NVInt elements.
    {
        int8_t buf[3] = {-5, 0, 5};
        Value v = nv_box_tensor((int64_t)(intptr_t)buf, 1, 1, 1,
                                3, 0, 0, 0, 0, 0, 0, 0);
        if (!v.obj) fail(NAME, "i8 case: expected a map value");
        NVArray* data = box_field_array(&v, "__data__", NAME);
        if (data->size != 3) fail(NAME, "i8 case: __data__ size != 3");
        const int32_t exp[3] = {-5, 0, 5};
        for (int i = 0; i < 3; i++)
            if (!is_int_obj(data->elements[i].obj, exp[i]))
                fail(NAME, "i8 case: element value mismatch");
    }

    // 6) Unsupported element size → null value (safety path).
    {
        float buf[2] = {1.0f, 2.0f};
        Value v = nv_box_tensor((int64_t)(intptr_t)buf, 1, 3, 2,
                                2, 0, 0, 0, 0, 0, 0, 0);
        if (v.obj)
            fail(NAME, "elem_size 3 must produce a null value");
    }

    // 7) Zero elements (d0 = 0) → map with empty __data__ and shape [0].
    {
        float buf[1] = {0.0f};
        Value v = nv_box_tensor((int64_t)(intptr_t)buf, 1, 4, 2,
                                0, 0, 0, 0, 0, 0, 0, 0);
        if (!v.obj || v.obj->ob_type != NVMap_Type)
            fail(NAME, "empty case: expected a map value");
        NVArray* data  = box_field_array(&v, "__data__", NAME);
        NVArray* shape = box_field_array(&v, "__shape__", NAME);
        if (data->size != 0) fail(NAME, "empty case: __data__ not empty");
        if (shape->size != 1 || !is_int_obj(shape->elements[0].obj, 0))
            fail(NAME, "empty case: wrong shape");
    }

    ok(NAME);
}

int main() {
    std::puts("=== NIR Pipeline Tests ===");
    std::fflush(stdout);

    test_infrastructure();
    test_alloc_drop();
    test_tensor_to_value_pipeline();
    test_nv_box_tensor_runtime();

    std::puts("=== All NIR tests PASSED ===");
    std::fflush(stdout);
    return 0;
}
