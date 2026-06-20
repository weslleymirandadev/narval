#pragma once

#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/PatternMatch.h"

namespace mlir::narval {

// Create a tensor.empty + linalg.fill for a given ranked tensor type.
inline Value make_empty_tensor(PatternRewriter& r, Location loc,
                                RankedTensorType type) {
    SmallVector<int64_t> static_shape(type.getShape());
    return tensor::EmptyOp::create(r, loc, static_shape,
                                    type.getElementType()).getResult();
}

// Inline a region's body into a linalg generic/reduce body.
inline LogicalResult inline_linalg_body(Region& source, OpBuilder& b,
                                         Location loc, ValueRange args) {
    if (source.empty() || source.front().empty())
        return failure();
    Block& src_block = source.front();
    if (src_block.getNumArguments() != args.size())
        return failure();
    IRMapping mapping;
    for (auto [from, to] : llvm::zip(src_block.getArguments(), args))
        mapping.map(from, to);
    for (Operation& nested : src_block.without_terminator())
        b.clone(nested, mapping);
    auto yield = dyn_cast<::mlir::linalg::YieldOp>(src_block.getTerminator());
    if (!yield || yield.getOperands().empty())
        return failure();
    SmallVector<Value> yielded;
    for (Value value : yield.getOperands())
        yielded.push_back(mapping.lookupOrDefault(value));
    ::mlir::linalg::YieldOp::create(b, loc, yielded);
    return success();
}

} // namespace mlir::narval
