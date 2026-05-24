#include "backend/nir/NarvalPasses.h"
#include "backend/nir/NarvalTypes.h"

#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVM.h"
#include "mlir/Conversion/LLVMCommon/ConversionTarget.h"
#include "mlir/Conversion/LLVMCommon/TypeConverter.h"
#include "mlir/Conversion/MemRefToLLVM/MemRefToLLVM.h"
#include "mlir/Conversion/UBToLLVM/UBToLLVM.h"
#include "mlir/Conversion/VectorToLLVM/ConvertVectorToLLVM.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#define GEN_PASS_DEF_LOWERNARVALTOLLVMPASS
#include "NarvalPasses.h.inc"

using namespace mlir;

namespace nv {
namespace {

struct LowerNarvalToLLVMPassImpl
    : public ::impl::LowerNarvalToLLVMPassBase<LowerNarvalToLLVMPassImpl> {

    void runOnOperation() override {
        ModuleOp module = getOperation();
        MLIRContext* ctx = &getContext();

        LLVMTypeConverter type_converter(ctx);
        // Teach the LLVM converter how to handle residual !narval.value types
        // (e.g. in function declarations not yet converted by the std pass).
        auto ptr_ty = LLVM::LLVMPointerType::get(ctx);
        type_converter.addConversion([ptr_ty](narval::ValueType)   -> Type { return ptr_ty; });
        type_converter.addConversion([ptr_ty](narval::RefType)     -> Type { return ptr_ty; });
        type_converter.addConversion([ptr_ty](narval::MutRefType)  -> Type { return ptr_ty; });

        LLVMConversionTarget target(*ctx);
        target.addLegalOp<ModuleOp>();

        RewritePatternSet patterns(ctx);
        arith::populateArithToLLVMConversionPatterns(type_converter, patterns);
        populateFuncToLLVMConversionPatterns(type_converter, patterns);
        cf::populateControlFlowToLLVMConversionPatterns(type_converter, patterns);
        populateFinalizeMemRefToLLVMConversionPatterns(type_converter, patterns);
        ub::populateUBToLLVMConversionPatterns(type_converter, patterns);
        populateVectorToLLVMConversionPatterns(type_converter, patterns, false);

        if (failed(applyFullConversion(module, target, std::move(patterns)))) {
            module.emitError("lower-narval-to-llvm: full conversion to LLVM failed");
            signalPassFailure();
        }
    }
};

} // namespace

std::unique_ptr<mlir::Pass> createLowerNarvalToLLVMPass() {
    return std::make_unique<LowerNarvalToLLVMPassImpl>();
}

} // namespace nv
