#include "backend/nir/NIRGenerationContext.hpp"
#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalTypes.h"
#include "backend/nir/NarvalPasses.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <cstdlib>

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

int main() {
    std::puts("=== NIR Pipeline Tests ===");
    std::fflush(stdout);

    test_infrastructure();
    test_alloc_drop();

    std::puts("=== All NIR tests PASSED ===");
    std::fflush(stdout);
    return 0;
}
