#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#define GEN_PASS_DEF_NARVALOWNERSHIPPASS
#include "NarvalPasses.h.inc"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

static bool escapes(Value value) {
    for (Operation* user : value.getUsers()) {
        if (isa<func::ReturnOp, func::CallOp, CallOp, CallRuntimeOp, ReturnOp>(user))
            return true;
    }
    return false;
}

static Operation* last_use_in_block(Value value, Block* block) {
    Operation* last = nullptr;
    for (Operation* user : value.getUsers()) {
        if (user->getBlock() != block) continue;
        if (!last || last->isBeforeInBlock(user)) last = user;
    }
    return last;
}

static void process_func(func::FuncOp func) {
    if (func.empty()) return;
    Block& entry = func.front();

    SmallVector<AllocOp> allocs;
    for (auto& op : entry.getOperations()) {
        if (auto alloc = dyn_cast<AllocOp>(&op))
            allocs.push_back(alloc);
    }

    for (AllocOp alloc : allocs) {
        Value v = alloc.getResult();
        if (escapes(v)) continue;

        bool already_dropped = false;
        for (Operation* user : v.getUsers())
            if (isa<DropOp>(user)) { already_dropped = true; break; }
        if (already_dropped) continue;

        Block* def_block = alloc->getBlock();
        Operation* last  = last_use_in_block(v, def_block);

        OpBuilder builder(alloc->getContext());
        if (last) builder.setInsertionPointAfter(last);
        else       builder.setInsertionPoint(def_block->getTerminator());

        DropOp::create(builder, alloc.getLoc(), v);
    }
}

struct NarvalOwnershipPassImpl
    : public ::impl::NarvalOwnershipPassBase<NarvalOwnershipPassImpl> {

    void runOnOperation() override {
        ModuleOp module = getOperation();
        // Iterate over top-level ops by op name (avoids TypeID-lookup hang with
        // Threading::DISABLED in MLIR 22).
        for (Operation& op : module.getBody()->getOperations()) {
            if (op.getName().getStringRef() != "func.func") continue;
            if (auto func = dyn_cast<func::FuncOp>(op))
                process_func(func);
        }
    }
};

} // namespace

std::unique_ptr<mlir::Pass> createNarvalOwnershipPass() {
    return std::make_unique<NarvalOwnershipPassImpl>();
}

} // namespace nv
