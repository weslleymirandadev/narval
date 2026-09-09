#include "backend/nir/NarvalOps.h"
#include "backend/nir/passes/NarvalTypeConverter.hpp"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Bufferization/IR/Bufferization.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

//===----------------------------------------------------------------------===//
// narval.tensor_to_value → nv_box_tensor runtime call
//===----------------------------------------------------------------------===//
//
// Lowers narval.tensor_to_value by:
//   1. Allocating a memref for the input tensor
//   2. Copying the tensor data into it via bufferization.materialize_in_destination
//      (memref.tensor_store was removed in MLIR >= 20; this is the canonical
//      pre-bufferization op for forcing a tensor into an existing memref)
//   3. Extracting the data pointer and shape, then calling
//      nv_box_tensor(ptr_i64, ndim_i64, elem_size_i64, dtype_i64, d0_i64..d7_i64)
// The runtime function reads elements from the flat buffer and creates
// a boxed Value (NVMap with __data__ and __shape__ entries).
//
// Runs in LowerNarvalToStandardPass (phase A); the produced memref ops +
// materialize_in_destination are resolved by OneShotBufferize in phase B.

struct LowerTensorToValue : public OpConversionPattern<TensorToValueOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(TensorToValueOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        (void)adaptor;  // tensors are not converted by NarvalTypeConverter
        Location loc = op.getLoc();
        MLIRContext* ctx = r.getContext();
        ModuleOp mod = op->getParentOfType<ModuleOp>();

        // 1. Extract tensor type info
        auto tensorType = mlir::dyn_cast<RankedTensorType>(op.getInput().getType());
        if (!tensorType)
            return failure();
        Type elemType = tensorType.getElementType();
        ArrayRef<int64_t> shape = tensorType.getShape();

        // 2. Compute element size in bytes + runtime dtype tag.
        //    dtype disambiguates f32 vs i32 (same byte width); the memref
        //    element type is known at compile time, so no runtime guessing.
        int64_t elemSize = 0;
        int64_t dtype = 0;  // NV_INT_BASE=1 / NV_FLOAT_BASE=2
        if (elemType.isF16() || elemType.isBF16()) {
            elemSize = 2; dtype = 2;
        } else if (elemType.isF32()) {
            elemSize = 4; dtype = 2;
        } else if (elemType.isF64()) {
            elemSize = 8; dtype = 2;
        } else if (elemType.isInteger(8)) {
            elemSize = 1; dtype = 1;
        } else if (elemType.isInteger(16)) {
            elemSize = 2; dtype = 1;
        } else if (elemType.isInteger(32)) {
            elemSize = 4; dtype = 1;
        } else if (elemType.isInteger(64)) {
            // NVInt is int32 — i64 boxing is not representable in the runtime
            elemSize = 8; dtype = 1;
            return op.emitError("i64 tensors cannot be boxed yet (NVInt is int32)");
        } else {
            return op.emitError("unsupported element type for tensor_to_value");
        }

        // 3. Create memref type matching the tensor shape + element type
        auto memrefType = MemRefType::get(shape, elemType);

        // 4. Allocate memref buffer
        Value alloc = r.create<memref::AllocOp>(loc, memrefType);

        // 5. Copy tensor data into the allocated memref.
        //    memref.tensor_store was removed in MLIR >= 20; the canonical
        //    pre-bufferization way to force a tensor into an existing memref is
        //    bufferization.materialize_in_destination (memref dest requires the
        //    `writable` attribute). OneShotBufferize (phase B) resolves it to a
        //    memcpy into `alloc`.
        r.create<bufferization::MaterializeInDestinationOp>(
            loc, /*result=*/mlir::Type(), op.getInput(), alloc,
            /*restrict=*/mlir::UnitAttr(), /*writable=*/mlir::UnitAttr::get(ctx));

        // 6. Get the data pointer as an index value
        Value ptrIndex = r.create<memref::ExtractAlignedPointerAsIndexOp>(
            loc, alloc);

        // 7. Convert the index pointer and element size to i64
        IntegerType i64Ty = r.getI64Type();
        Value ptrI64 = r.create<arith::IndexCastOp>(loc, i64Ty, ptrIndex);
        Value elemSizeVal = r.create<arith::ConstantOp>(
            loc, IntegerAttr::get(i64Ty, elemSize));
        Value dtypeVal = r.create<arith::ConstantOp>(
            loc, IntegerAttr::get(i64Ty, dtype));

        // 8. Build ndim and per-dimension sizes as i64 values
        Value ndim = r.create<arith::ConstantOp>(
            loc, IntegerAttr::get(i64Ty, shape.size()));

        // 9. Assemble call arguments: ptr, ndim, elem_size, dtype, d0, d1, ...
        SmallVector<Value> callArgs;
        callArgs.push_back(ptrI64);
        callArgs.push_back(ndim);
        callArgs.push_back(elemSizeVal);
        callArgs.push_back(dtypeVal);
        for (size_t i = 0; i < shape.size(); i++) {
            Value dimVal = r.create<memref::DimOp>(loc, alloc, i);
            Value dimI64 = r.create<arith::IndexCastOp>(loc, i64Ty, dimVal);
            callArgs.push_back(dimI64);
        }
        // Pad remaining dim slots with 0. nv_box_tensor's C signature is
        // (ptr, ndim, elem_size, dtype, d0..d7) — 12 fixed args total.
        while (callArgs.size() < 12) {
            callArgs.push_back(
                r.create<arith::ConstantOp>(loc, IntegerAttr::get(i64Ty, 0)));
        }

        // 10. Declare and call the runtime function. Runtime functions that
        //     return Value are declared as returning !llvm.ptr (ABI: the Value
        //     struct is a single pointer) — same convention as nv_box_int &
        //     friends in LowerConstantOp.cpp.
        //     Signature: (i64 x 12) -> !llvm.ptr
        SmallVector<Type> ftArgs(12, i64Ty);
        Type ptrTy = LLVM::LLVMPointerType::get(ctx);
        FunctionType ft = FunctionType::get(ctx, ftArgs, {ptrTy});
        ensure_decl(mod, r, "nv_box_tensor", ft);

        // 11. Replace the op with the runtime call result. The op's
        //     !narval.value result type is converted to !llvm.ptr by the
        //     NarvalTypeConverter — this is an OpConversionPattern.
        Value result = func::CallOp::create(r, loc, TypeRange{ptrTy},
                                            "nv_box_tensor", callArgs)
                           .getResult(0);
        r.replaceOp(op, result);
        return success();
    }
};

} // namespace

void populateLowerTensorToValue(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc) {
    patterns.add<LowerTensorToValue>(tc, patterns.getContext());
}

} // namespace nv
