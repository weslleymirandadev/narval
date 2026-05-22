#ifdef NARVAL_USE_NIR

#include "backend/codegen/tensor_nir.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "backend/nir/NIRGenerationContext.hpp"

#include "mlir/Conversion/LinalgToStandard/LinalgToStandard.h"
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVM.h"
#include "mlir/Conversion/MemRefToLLVM/MemRefToLLVM.h"
#include "mlir/Conversion/LLVMCommon/ConversionTarget.h"
#include "mlir/Conversion/LLVMCommon/TypeConverter.h"
#include "mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/Linalg/Passes.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/LLVMIR/ModuleTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Transforms/DialectConversion.h"
#include "backend/nir/NarvalPasses.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/raw_ostream.h"

#include <sstream>
#include <string>
#include <vector>

namespace nv {

// Forward declarations
static std::unique_ptr<llvm::Module>
lower_kernel_module(mlir::MLIRContext& ctx, mlir::ModuleOp module,
                    llvm::LLVMContext& llvm_ctx, bool has_linalg,
                    const std::string& dbg_name);

// ── Naming ────────────────────────────────────────────────────────────────────

static std::string ds(int64_t d) { return d < 0 ? "d" : std::to_string(d); }

static std::string matmul_fn_name(int64_t M, int64_t K, int64_t N) {
    return "__nv_mm_f64_" + ds(M) + "x" + ds(K) + "x" + ds(N);
}
static std::string elemwise_fn_name(const std::string& op, int64_t n) {
    return "__nv_ew_" + op + "_f64_" + ds(n);
}

// ── MLIR kernel builders ──────────────────────────────────────────────────────

// Build matmul kernel: void fn(ptr A, ptr B, ptr C, i64 M, i64 K, i64 N)
// C[i,j] = sum_k A[i*K+k] * B[k*N+j]   (row-major double arrays, C pre-zeroed)
static std::unique_ptr<llvm::Module>
build_matmul_module(llvm::LLVMContext& llvm_ctx,
                    int64_t M, int64_t K, int64_t N,
                    const std::string& fn_name) {
    mlir::MLIRContext mlir_ctx(mlir::MLIRContext::Threading::DISABLED);
    nv::NIRGenerationContext nir(mlir_ctx, "<matmul>");
    auto& b   = nir.get_builder();
    auto  loc = b.getUnknownLoc();

    auto f64 = b.getF64Type();
    auto i64 = b.getI64Type();
    auto idx = b.getIndexType();
    auto ptr = mlir::LLVM::LLVMPointerType::get(&mlir_ctx);

    // void fn(ptr A, ptr B, ptr C, i64 M, i64 K, i64 N)
    auto fn_type = mlir::FunctionType::get(&mlir_ctx,
                       {ptr, ptr, ptr, i64, i64, i64}, {});
    auto fn = mlir::func::FuncOp::create(b, loc, fn_name, fn_type);
    fn.setPublic();
    auto* entry = fn.addEntryBlock();
    b.setInsertionPointToStart(entry);

    auto A_ptr = fn.getArgument(0);
    auto B_ptr = fn.getArgument(1);
    auto C_ptr = fn.getArgument(2);
    auto dyn_M = fn.getArgument(3);
    auto dyn_K = fn.getArgument(4);
    auto dyn_N = fn.getArgument(5);

    auto c0 = mlir::arith::ConstantIndexOp::create(b, loc, 0);
    auto c1 = mlir::arith::ConstantIndexOp::create(b, loc, 1);

    // Prefer static constants; fall back to casting the i64 arg.
    auto to_idx = [&](int64_t s, mlir::Value dyn) -> mlir::Value {
        if (s >= 0)
            return mlir::arith::ConstantIndexOp::create(b, loc, s);
        return mlir::arith::IndexCastOp::create(b, loc, idx, dyn);
    };
    auto bM = to_idx(M, dyn_M);
    auto bK = to_idx(K, dyn_K);
    auto bN = to_idx(N, dyn_N);

    // i64 K and N for GEP offset arithmetic
    auto iK = (K >= 0)
        ? (mlir::Value)mlir::arith::ConstantOp::create(b, loc, i64, b.getI64IntegerAttr(K))
        : dyn_K;
    auto iN = (N >= 0)
        ? (mlir::Value)mlir::arith::ConstantOp::create(b, loc, i64, b.getI64IntegerAttr(N))
        : dyn_N;

    // Loop i = 0..M
    auto iLoop = mlir::scf::ForOp::create(b, loc, c0, bM, c1);
    b.setInsertionPointToStart(iLoop.getBody());
    {
        mlir::Value i64i = mlir::arith::IndexCastOp::create(b, loc, i64, iLoop.getInductionVar());

        // Loop j = 0..N
        auto jLoop = mlir::scf::ForOp::create(b, loc, c0, bN, c1);
        b.setInsertionPointToStart(jLoop.getBody());
        {
            mlir::Value i64j = mlir::arith::IndexCastOp::create(b, loc, i64, jLoop.getInductionVar());

            // acc starts at 0.0; k loop carries it as iter_arg
            mlir::Value acc0 = mlir::arith::ConstantOp::create(b, loc, f64, b.getF64FloatAttr(0.0));
            mlir::SmallVector<mlir::Value> iter{acc0};
            auto kLoop = mlir::scf::ForOp::create(b, loc, c0, bK, c1, mlir::ValueRange(iter));
            b.setInsertionPointToStart(kLoop.getBody());
            {
                mlir::Value i64k = mlir::arith::IndexCastOp::create(b, loc, i64, kLoop.getInductionVar());
                mlir::Value acc  = kLoop.getRegionIterArgs()[0];

                // A[i*K + k]
                mlir::Value a_off = mlir::arith::AddIOp::create(b, loc,
                    mlir::Value(mlir::arith::MulIOp::create(b, loc, i64i, iK)), i64k);
                mlir::Value a_gep = mlir::LLVM::GEPOp::create(b, loc, ptr, f64, A_ptr,
                    mlir::SmallVector<mlir::LLVM::GEPArg>{a_off});
                mlir::Value a_val = mlir::LLVM::LoadOp::create(b, loc, f64, a_gep);

                // B[k*N + j]
                mlir::Value b_off = mlir::arith::AddIOp::create(b, loc,
                    mlir::Value(mlir::arith::MulIOp::create(b, loc, i64k, iN)), i64j);
                mlir::Value b_gep = mlir::LLVM::GEPOp::create(b, loc, ptr, f64, B_ptr,
                    mlir::SmallVector<mlir::LLVM::GEPArg>{b_off});
                mlir::Value b_val = mlir::LLVM::LoadOp::create(b, loc, f64, b_gep);

                // new_acc = acc + a * b
                mlir::Value prod    = mlir::arith::MulFOp::create(b, loc, a_val, b_val);
                mlir::Value new_acc = mlir::arith::AddFOp::create(b, loc, acc, prod);
                mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{new_acc});
            }
            // Store C[i*N + j] = accumulated dot product
            b.setInsertionPointAfter(kLoop);
            mlir::Value c_off = mlir::arith::AddIOp::create(b, loc,
                mlir::Value(mlir::arith::MulIOp::create(b, loc, i64i, iN)), i64j);
            mlir::Value c_gep = mlir::LLVM::GEPOp::create(b, loc, ptr, f64, C_ptr,
                mlir::SmallVector<mlir::LLVM::GEPArg>{c_off});
            mlir::LLVM::StoreOp::create(b, loc, kLoop.getResult(0), c_gep);
            mlir::scf::YieldOp::create(b, loc);
        }
        b.setInsertionPointAfter(jLoop);
        mlir::scf::YieldOp::create(b, loc);
    }
    b.setInsertionPointAfter(iLoop);
    mlir::func::ReturnOp::create(b, loc);

    // has_linalg=true so createConvertLinalgToLoopsPass runs first — required to
    // warm up TypeID resolution for createSCFToControlFlowPass in Threading::DISABLED.
    return lower_kernel_module(mlir_ctx, nir.get_module(), llvm_ctx, true, fn_name);
}

// Build element-wise kernel: void fn(ptr A, ptr B, ptr C, i64 N)
// C[i] = A[i] OP B[i]
static std::unique_ptr<llvm::Module>
build_elemwise_module(llvm::LLVMContext& llvm_ctx,
                      const std::string& op,
                      int64_t n_elems,
                      const std::string& fn_name) {
    mlir::MLIRContext mlir_ctx(mlir::MLIRContext::Threading::DISABLED);
    nv::NIRGenerationContext nir(mlir_ctx, "<elemwise_" + op + ">");
    auto& b   = nir.get_builder();
    auto  loc = b.getUnknownLoc();

    auto f64 = b.getF64Type();
    auto i64 = b.getI64Type();
    auto idx = b.getIndexType();
    auto ptr = mlir::LLVM::LLVMPointerType::get(&mlir_ctx);

    auto fn_type = mlir::FunctionType::get(&mlir_ctx, {ptr, ptr, ptr, i64}, {});
    auto fn = mlir::func::FuncOp::create(b, loc, fn_name, fn_type);
    fn.setPublic();
    auto* entry = fn.addEntryBlock();
    b.setInsertionPointToStart(entry);

    auto A_ptr = fn.getArgument(0);
    auto B_ptr = fn.getArgument(1);
    auto C_ptr = fn.getArgument(2);
    auto dyn_N = fn.getArgument(3);

    auto c0 = mlir::arith::ConstantIndexOp::create(b, loc, 0);
    auto c1 = mlir::arith::ConstantIndexOp::create(b, loc, 1);
    auto bN = (n_elems >= 0)
        ? (mlir::Value)mlir::arith::ConstantIndexOp::create(b, loc, n_elems)
        : (mlir::Value)mlir::arith::IndexCastOp::create(b, loc, idx, dyn_N);

    auto loop = mlir::scf::ForOp::create(b, loc, c0, bN, c1);
    b.setInsertionPointToStart(loop.getBody());
    {
        mlir::Value i64i = mlir::arith::IndexCastOp::create(b, loc, i64, loop.getInductionVar());

        mlir::Value a_gep = mlir::LLVM::GEPOp::create(b, loc, ptr, f64, A_ptr,
            mlir::SmallVector<mlir::LLVM::GEPArg>{i64i});
        mlir::Value b_gep = mlir::LLVM::GEPOp::create(b, loc, ptr, f64, B_ptr,
            mlir::SmallVector<mlir::LLVM::GEPArg>{i64i});
        mlir::Value c_gep = mlir::LLVM::GEPOp::create(b, loc, ptr, f64, C_ptr,
            mlir::SmallVector<mlir::LLVM::GEPArg>{i64i});

        mlir::Value a_val = mlir::LLVM::LoadOp::create(b, loc, f64, a_gep);
        mlir::Value b_val = mlir::LLVM::LoadOp::create(b, loc, f64, b_gep);

        mlir::Value res;
        if (op == "sub")
            res = mlir::arith::SubFOp::create(b, loc, a_val, b_val);
        else if (op == "mul")
            res = mlir::arith::MulFOp::create(b, loc, a_val, b_val);
        else
            res = mlir::arith::AddFOp::create(b, loc, a_val, b_val);  // "add"

        mlir::LLVM::StoreOp::create(b, loc, res, c_gep);
        mlir::scf::YieldOp::create(b, loc);
    }
    b.setInsertionPointAfter(loop);
    mlir::func::ReturnOp::create(b, loc);

    return lower_kernel_module(mlir_ctx, nir.get_module(), llvm_ctx, true, fn_name);
}

// ── Shared IR helpers ─────────────────────────────────────────────────────────

// Link an MLIR-generated llvm::Module into the main module.
// Returns the function pointer, or nullptr on failure.
static llvm::Function*
link_kernel(nv::IRGenerationContext& ctx, const std::string& fn_name,
            std::unique_ptr<llvm::Module> mlir_mod) {
    if (!mlir_mod) return nullptr;
    if (llvm::Linker::linkModules(ctx.get_module(), std::move(mlir_mod))) {
        llvm::errs() << "[NIR] linker error for " << fn_name << "\n";
        return nullptr;
    }
    return ctx.get_module().getFunction(fn_name);
}

// Extract the raw double* data pointer from a NVTensor boxed in a Value struct.
static llvm::Value*
tensor_data_ptr(nv::IRGenerationContext& ctx, llvm::Value* val) {
    auto& B      = ctx.get_builder();
    auto* ValTy  = nv::ir_utils::get_value_struct(ctx);
    auto* PtrTy  = llvm::PointerType::getUnqual(ctx.get_context());
    auto* vptr   = nv::ir_utils::get_value_ptr(ctx);

    llvm::Value* slot;
    if (val->getType() == ValTy) {
        slot = ctx.create_alloca(ValTy, "ten_slot");
        B.CreateStore(val, slot);
    } else {
        slot = val;  // already a Value*
    }
    auto* fn = ctx.ensure_runtime_func("nv_tensor_data_ptr", {vptr}, PtrTy);
    return B.CreateCall(fn, {slot}, "data_ptr");
}

// Allocate an output NVTensor of shape `dims` (dtype = NV_FLOAT_BASE = 2).
// Returns the Value struct (by value, not pointer).
static llvm::Value*
alloc_out_tensor(nv::IRGenerationContext& ctx,
                 const std::vector<int64_t>& dims) {
    auto& B      = ctx.get_builder();
    auto* I32    = llvm::Type::getInt32Ty(ctx.get_context());
    auto* I64    = llvm::Type::getInt64Ty(ctx.get_context());
    auto* PtrTy  = llvm::PointerType::getUnqual(ctx.get_context());
    auto* ValTy  = nv::ir_utils::get_value_struct(ctx);
    int32_t ndim = static_cast<int32_t>(dims.size());

    // Build shape array on the stack
    auto* ArrTy  = llvm::ArrayType::get(I64, ndim < 1 ? 1 : ndim);
    auto* arr    = ctx.create_alloca(ArrTy, "out_shape");
    for (int i = 0; i < ndim; ++i) {
        auto* gep = B.CreateGEP(ArrTy, arr,
            {llvm::ConstantInt::get(I64, 0), llvm::ConstantInt::get(I64, i)});
        B.CreateStore(llvm::ConstantInt::get(I64, dims[i] < 0 ? 0 : dims[i]), gep);
    }
    auto* shape_ptr = B.CreateGEP(ArrTy, arr,
        {llvm::ConstantInt::get(I64, 0), llvm::ConstantInt::get(I64, 0)});

    auto* fn = ctx.ensure_runtime_func("nv_tensor_zeros", {I32, I32, PtrTy}, ValTy);
    return B.CreateCall(fn,
        {llvm::ConstantInt::get(I32, 2),     // NV_FLOAT_BASE
         llvm::ConstantInt::get(I32, ndim),
         shape_ptr},
        "out_val");
}

// ── Shared: lower a tensor kernel module to llvm::Module ─────────────────────
// Minimal pipeline: (optionally linalg→loops) → scf→cf → arith+func+cf→LLVM.
// Avoids all narval-specific passes so plain SCF/linalg modules go through
// cleanly without triggering "illegal op" errors from narval conversion targets.

static std::unique_ptr<llvm::Module>
lower_kernel_module(mlir::MLIRContext& ctx, mlir::ModuleOp module,
                    llvm::LLVMContext& llvm_ctx, bool has_linalg,
                    const std::string& dbg_name) {
    mlir::PassManager pm(&ctx);
    pm.enableVerifier(false);

    // 1. Lower linalg named ops → scf loops (handles matmul + fill)
    if (has_linalg)
        pm.addPass(mlir::createConvertLinalgToLoopsPass());

    // 2. Lower scf → cf basic blocks
    pm.addPass(mlir::createSCFToControlFlowPass());

    // 3. Lower arith + func + cf + memref → LLVM dialect
    // Reuse the existing narval LLVM lowering pass (in nir_dialect) which does
    // exactly this: arith + func + cf + memref → LLVM via applyFullConversion.
    pm.addPass(nv::createLowerNarvalToLLVMPass());

    // 4. Clean up any unrealized casts left by the conversions
    pm.addPass(mlir::createReconcileUnrealizedCastsPass());

    if (mlir::failed(pm.run(module))) {
        llvm::errs() << "[NIR] kernel pipeline failed for " << dbg_name << "\n";
        return nullptr;
    }

    mlir::registerBuiltinDialectTranslation(ctx);
    mlir::registerLLVMDialectTranslation(ctx);
    auto llvm_mod = mlir::translateModuleToLLVMIR(module, llvm_ctx, dbg_name);
    if (!llvm_mod) {
        llvm::errs() << "[NIR] IR translation failed for " << dbg_name << "\n";
    }
    return llvm_mod;
}

// ── linalg.matmul kernel (AVX2 path) ─────────────────────────────────────────
//
// Builds a function taking 21 i64/ptr args (3 × unpacked memref<MxKxf64>):
//   base, aligned, offset, size0, size1, stride0, stride1   (×3 for A, B, C)
//
// MLIR's --convert-func-to-llvm unpacks memref<M×K×f64> into 7 scalars.
// linalg.matmul on static memrefs → convert-linalg-to-loops → LLVM.
// With -O2 the LLVM backend auto-vectorises the inner loop to AVX2.

static std::string linalg_mm_fn_name(int64_t M, int64_t K, int64_t N) {
    return "__nv_lmm_f64_" + ds(M) + "x" + ds(K) + "x" + ds(N);
}

static std::unique_ptr<llvm::Module>
build_linalg_matmul_module(llvm::LLVMContext& llvm_ctx,
                            int64_t M, int64_t K, int64_t N,
                            const std::string& fn_name) {
    mlir::MLIRContext mlir_ctx(mlir::MLIRContext::Threading::DISABLED);
    nv::NIRGenerationContext nir(mlir_ctx, "<linalg_matmul>");
    mlir_ctx.loadDialect<mlir::linalg::LinalgDialect>();

    auto& b   = nir.get_builder();
    auto  loc = b.getUnknownLoc();
    auto  f64 = b.getF64Type();

    // Static memref types
    auto mA = mlir::MemRefType::get({M, K}, f64);
    auto mB = mlir::MemRefType::get({K, N}, f64);
    auto mC = mlir::MemRefType::get({M, N}, f64);

    auto fn_type = mlir::FunctionType::get(&mlir_ctx, {mA, mB, mC}, {});
    auto fn = mlir::func::FuncOp::create(b, loc, fn_name, fn_type);
    fn.setPublic();
    auto* entry = fn.addEntryBlock();
    b.setInsertionPointToStart(entry);

    mlir::Value A = fn.getArgument(0);
    mlir::Value B = fn.getArgument(1);
    mlir::Value C = fn.getArgument(2);

    // Zero-fill C before accumulating
    mlir::Value zero = mlir::arith::ConstantOp::create(b, loc, f64, b.getF64FloatAttr(0.0));
    mlir::linalg::FillOp::create(b, loc, mlir::ValueRange{zero}, mlir::ValueRange{C});

    // linalg.matmul accumulates: C += A × B
    mlir::linalg::MatmulOp::create(b, loc,
        mlir::TypeRange{},
        mlir::ValueRange{A, B},
        mlir::ValueRange{C});

    mlir::func::ReturnOp::create(b, loc);

    return lower_kernel_module(mlir_ctx, nir.get_module(), llvm_ctx,
                               /*has_linalg=*/true, fn_name);
}

// ── Build and call the linalg matmul kernel from LLVM IR ─────────────────────
//
// MLIR unpacks memref<MxKxf64> into 7 args: ptr base, ptr aligned, i64 offset,
// i64 size0, i64 size1, i64 stride0, i64 stride1.
// We construct the 3×7 = 21-arg call from the raw data pointer + static shape.

static llvm::Value*
call_linalg_matmul(nv::IRGenerationContext& ctx,
                   llvm::Function* kernel,
                   llvm::Value*    A_data,
                   llvm::Value*    B_data,
                   llvm::Value*    C_data,
                   int64_t M, int64_t K, int64_t N) {
    auto& B   = ctx.get_builder();
    auto* I64 = llvm::Type::getInt64Ty(ctx.get_context());

    auto c = [&](int64_t v) { return llvm::ConstantInt::get(I64, v); };

    // 7 args per 2D row-major memref: base, aligned, offset=0, s0, s1, str0, str1
    auto memref_args = [&](llvm::Value* data, int64_t s0, int64_t s1)
        -> std::vector<llvm::Value*> {
        return {data, data, c(0), c(s0), c(s1), c(s1), c(1)};
    };

    std::vector<llvm::Value*> args;
    for (auto* v : memref_args(A_data, M, K)) args.push_back(v);
    for (auto* v : memref_args(B_data, K, N)) args.push_back(v);
    for (auto* v : memref_args(C_data, M, N)) args.push_back(v);

    B.CreateCall(kernel, args);
    return nullptr;  // caller returns C_val directly
}

// ── Public API ────────────────────────────────────────────────────────────────

llvm::Value* emit_tensor_matmul_nir(IRGenerationContext& ctx,
                                     llvm::Value* lhs_v,
                                     llvm::Value* rhs_v,
                                     const std::vector<int64_t>& lhs_dims,
                                     const std::vector<int64_t>& rhs_dims) {
    if (lhs_dims.size() < 2 || rhs_dims.size() < 2) return nullptr;

    int64_t M = lhs_dims[0], K = lhs_dims[1], N = rhs_dims[1];

    auto& B      = ctx.get_builder();
    auto* ValTy  = nv::ir_utils::get_value_struct(ctx);

    auto* A_data = tensor_data_ptr(ctx, lhs_v);
    auto* B_data = tensor_data_ptr(ctx, rhs_v);
    if (!A_data || !B_data) return nullptr;

    // Allocate output tensor C[M, N]  (pre-zeroed — linalg.fill also zeros it)
    std::vector<int64_t> c_dims = {M, N};
    auto  C_val  = alloc_out_tensor(ctx, c_dims);
    auto* C_slot = ctx.create_alloca(ValTy, "C_slot");
    B.CreateStore(C_val, C_slot);
    auto* C_data = tensor_data_ptr(ctx, C_slot);
    if (!C_data) return nullptr;

    // ── Try linalg.matmul kernel (AVX2 path) ──────────────────────────────────
    // Static shapes only (all dims >= 0). Dynamic dims fall through to SCF kernel.
    if (M >= 0 && K >= 0 && N >= 0) {
        std::string lmm_name = linalg_mm_fn_name(M, K, N);
        llvm::Function* lkernel = ctx.get_module().getFunction(lmm_name);
        if (!lkernel) {
            auto mod = build_linalg_matmul_module(ctx.get_context(), M, K, N, lmm_name);
            lkernel  = link_kernel(ctx, lmm_name, std::move(mod));
        }
        if (lkernel) {
            call_linalg_matmul(ctx, lkernel, A_data, B_data, C_data, M, K, N);
            return C_val;
        }
        // Fall through to SCF kernel if linalg build failed
    }

    // ── SCF loop kernel (fallback / dynamic shapes) ───────────────────────────
    auto* I64 = llvm::Type::getInt64Ty(ctx.get_context());
    std::string fn_name = matmul_fn_name(M, K, N);
    llvm::Function* kernel = ctx.get_module().getFunction(fn_name);
    if (!kernel) {
        auto mod = build_matmul_module(ctx.get_context(), M, K, N, fn_name);
        kernel   = link_kernel(ctx, fn_name, std::move(mod));
        if (!kernel) return nullptr;
    }

    B.CreateCall(kernel, {A_data, B_data, C_data,
        llvm::ConstantInt::get(I64, M < 0 ? 0 : M),
        llvm::ConstantInt::get(I64, K < 0 ? 0 : K),
        llvm::ConstantInt::get(I64, N < 0 ? 0 : N)});

    return C_val;
}

llvm::Value* emit_tensor_elemwise_nir(IRGenerationContext&,
                                       llvm::Value*,
                                       llvm::Value*,
                                       const std::vector<int64_t>&,
                                       const std::string&) {
    // Element-wise ops (add/sub/mul) use the C runtime directly.
    // The linalg.matmul path is where MLIR adds real value (semantic info for
    // the optimizer); for simple elementwise loops the C runtime is equivalent.
    return nullptr;
}

} // namespace nv

#else
// NARVAL_USE_NIR is off — this TU is empty.
#endif // NARVAL_USE_NIR
