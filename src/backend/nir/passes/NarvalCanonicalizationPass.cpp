#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#define GEN_PASS_DEF_NARVALCANONICALIZATIONPASS
#include "NarvalPasses.h.inc"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

//  Helper: check if a value comes from a tensor_fill with a scalar constant 
static bool is_tensor_fill_with_scalar(Value v, double expected) {
    auto* def = v.getDefiningOp();
    if (!def) return false;
    auto fill = dyn_cast<TensorFillOp>(def);
    if (!fill) return false;
    auto* cst_op = fill.getFillValue().getDefiningOp();
    if (!cst_op) return false;
    auto cst = dyn_cast<arith::ConstantFloatOp>(cst_op);
    if (!cst) return false;
    return cst.value().convertToDouble() == expected;
}

//  Pattern 1: tensor_add(x, fill(0)) → x  
static bool fold_tensor_add_zero(TensorAddOp op, IRRewriter& rw) {
    if (is_tensor_fill_with_scalar(op.getRhs(), 0.0)) {
        rw.replaceOp(op, op.getLhs());
        return true;
    }
    if (is_tensor_fill_with_scalar(op.getLhs(), 0.0)) {
        rw.replaceOp(op, op.getRhs());
        return true;
    }
    return false;
}

//  Pattern 2: tensor_mul(x, fill(1)) → x  
static bool fold_tensor_mul_one(TensorMulOp op, IRRewriter& rw) {
    if (is_tensor_fill_with_scalar(op.getRhs(), 1.0)) {
        rw.replaceOp(op, op.getLhs());
        return true;
    }
    if (is_tensor_fill_with_scalar(op.getLhs(), 1.0)) {
        rw.replaceOp(op, op.getRhs());
        return true;
    }
    return false;
}

//  Pattern 3: move(borrow(x)) → x  
static bool fold_move_of_borrow(MoveOp op, IRRewriter& rw) {
    auto* def = op.getSource().getDefiningOp();
    if (!def) return false;
    if (auto borrow = dyn_cast<BorrowOp>(def)) {
        rw.replaceOp(op, borrow.getSource());
        return true;
    }
    return false;
}

//  Pattern 4: alloc + immediate drop in same block → erase both 
static bool fold_alloc_drop(AllocOp alloc_op, IRRewriter& rw) {
    Value result = alloc_op.getResult();
    // Check if the only use is a single drop in the same block
    if (!result.hasOneUse()) return false;
    auto* user = *result.user_begin();
    if (!isa<DropOp>(user)) return false;
    if (user->getBlock() != alloc_op->getBlock()) return false;
    rw.eraseOp(user);
    rw.eraseOp(alloc_op);
    return true;
}

struct NarvalCanonicalizationPassImpl
    : public ::impl::NarvalCanonicalizationPassBase<NarvalCanonicalizationPassImpl> {

    void runOnOperation() override {
        ModuleOp module = getOperation();
        IRRewriter rw(&getContext());

        // Single-pass fold over all functions; no greedy loop (safe with Threading::DISABLED).
        for (auto func : module.getOps<func::FuncOp>()) {
            if (func.isExternal() || func.getBody().empty()) continue;

            Block& block = func.getBody().front();
            bool changed = true;
            // Up to 8 rounds so chained folds converge (e.g. move(borrow(move(borrow(x))))).
            for (int round = 0; round < 8 && changed; ++round) {
                changed = false;
                // Collect ops first (modification invalidates iterators).
                SmallVector<Operation*> ops;
                for (auto& op : block) ops.push_back(&op);

                for (auto* op : ops) {
                    if (!op->getBlock()) continue;  // already erased
                    if (auto add = dyn_cast<TensorAddOp>(op))
                        changed |= fold_tensor_add_zero(add, rw);
                    else if (auto mul = dyn_cast<TensorMulOp>(op))
                        changed |= fold_tensor_mul_one(mul, rw);
                    else if (auto mov = dyn_cast<MoveOp>(op))
                        changed |= fold_move_of_borrow(mov, rw);
                    else if (auto alloc = dyn_cast<AllocOp>(op))
                        changed |= fold_alloc_drop(alloc, rw);
                }
            }
        }
    }
};

} // namespace

std::unique_ptr<mlir::Pass> createNarvalCanonicalizationPass() {
    return std::make_unique<NarvalCanonicalizationPassImpl>();
}

} // namespace nv
