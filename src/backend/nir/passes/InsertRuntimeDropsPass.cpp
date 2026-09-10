// InsertRuntimeDropsPass: frees runtime heap temporaries.
//
// Every runtime call (nv_box_*, nv_add, nv_index_to_value, ...) returns a
// freshly allocated NvObject* with ref_count == 1, and until now nothing ever
// dropped them: a loop body allocating a few boxes per iteration grew memory
// linearly (1M iterations ≈ 64 MB, 10M ≈ 626 MB — measured).
//
// The runtime has full refcounting (nv_incref/nv_decref + tp_dealloc) but the
// compiled code never calls it. This pass runs AFTER lower-narval-to-std
// (values are !llvm.ptr by then) and inserts `call @nv_drop(%v)` right after
// the LAST USE of every pointer value whose uses all live in its defining
// block and that does not flow through a terminator (cf.br/cond_br — branch
// operands become the destination block's args, so the destination owns the
// object and drops it when IT dies) and does not escape through func.return.
//
// Conservative v1: values used across blocks (e.g. produced before an if and
// used in its branches) are left alone (they may still leak; they can never
// double-free). Loop-carried objects are dropped by the block-arg rule: the
// body block's carried arg dies after its last use in the body.
//
// The function-entry block args are intentionally not dropped: the caller
// owns the objects it passes (and drops them after the call).

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include <string>

using namespace mlir;

namespace nv {
namespace {

struct InsertRuntimeDropsPass
    : public PassWrapper<InsertRuntimeDropsPass, OperationPass<ModuleOp>> {
    MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(InsertRuntimeDropsPass)

    StringRef getArgument() const final {
        return "narval-insert-runtime-drops";
    }
    StringRef getDescription() const final {
        return "Insert nv_drop calls after the last use of runtime heap "
               "temporaries";
    }

    static func::FuncOp get_or_create_drop_fn(ModuleOp module, OpBuilder& b) {
        for (Operation& op : module.getBody()->getOperations())
            if (auto f = dyn_cast<func::FuncOp>(op))
                if (f.getName() == "nv_drop") return f;
        b.setInsertionPointToStart(module.getBody());
        auto fn = b.create<func::FuncOp>(
            b.getUnknownLoc(), "nv_drop",
            FunctionType::get(module.getContext(),
                              {LLVM::LLVMPointerType::get(module.getContext())},
                              {}));
        fn.setPrivate();
        return fn;
    }

    // Runtime calls whose result is a NEW heap object (ref_count == 1) owned
    // by the caller. Results of accessors (nv_get_at, nv_container_get,
    // nv_len...) are pointers INTO containers and must not be dropped.
    static bool owned_ret(const std::string& name) {
        if (name.rfind("nv_box_", 0) == 0) return true;
        if (name == "nv_index_to_value") return true;
        if (name.rfind("nv_create_", 0) == 0) return true;
        static const char* const arith[] = {
            "nv_add", "nv_sub", "nv_mul", "nv_div", "nv_mod",
        };
        for (const char* a : arith)
            if (name == a) return true;
        return false;
    }

    // Calls that STORE an argument into a container without incref'ing it
    // (the container takes ownership). A value whose last use is such a call
    // must not be dropped by the caller.
    static bool guards_args(const std::string& name) {
        if (name == "nv_set_field") return true;
        if (name.find("push") != std::string::npos) return true;
        if (name.find("set_") != std::string::npos &&
            name != "nv_set_global") return true;
        // create_closure_cN snapshots its capture arguments into cells.
        if (name.rfind("nv_create_closure", 0) == 0) return true;
        return false;
    }

    static void try_drop(Value v, Block* def_block, OpBuilder& b,
                         func::FuncOp drop_fn, bool from_call) {
        if (!isa<LLVM::LLVMPointerType>(v.getType())) return;
        // Only drop heap NvObject*. Data pointers (string-literal addresses,
        // memref bases) are !llvm.ptr too but are NOT runtime objects — they
        // come from addressof/getelementptr ops, never from func.call. And
        // only drop results of owning calls (see owned_ret). Block args are
        // carried runtime values and are always owned.
        if (!from_call) return;
        Operation* prod = v.getDefiningOp();
        if (prod) {
            auto call = dyn_cast<func::CallOp>(prod);
            if (!call) return;
            if (!owned_ret(call.getCallee().str())) return;
        }

        Operation* last = nullptr;
        bool foreign = false;
        bool flows = false;
        bool stored = false;
        for (Operation* user : v.getUsers()) {
            if (isa<func::ReturnOp>(user)) return;  // escapes as the result
            if (user->getBlock() != def_block) {
                foreign = true;
                continue;
            }
            // Branch operands carry the value into the destination block's
            // args — the destination owns the object from there.
            if (user->hasTrait<OpTrait::IsTerminator>()) {
                flows = true;
                continue;
            }
            // ANY use that stores the value into a container transfers
            // ownership to the container (it holds the pointer without an
            // incref) — the caller must not drop it afterwards. Same for
            // calls to USER functions: the callee may store the argument
            // anywhere (closure capture, container) and there is no incref
            // on the ABI boundary, so be conservative.
            if (auto uc = dyn_cast<func::CallOp>(user)) {
                std::string cname = uc.getCallee().str();
                if (guards_args(cname) ||
                    cname.rfind("nv_", 0) != 0) {
                    stored = true;
                    continue;
                }
            }
            if (!last || last->isBeforeInBlock(user)) last = user;
        }
        if (foreign || flows || stored || !last) return;

        b.setInsertionPointAfter(last);
        b.create<func::CallOp>(last->getLoc(), drop_fn, ValueRange{v});
    }

    void runOnOperation() override {
        ModuleOp module = getOperation();
        OpBuilder b(module.getContext());
        func::FuncOp drop_fn = get_or_create_drop_fn(module, b);

        for (Operation& op : module.getBody()->getOperations()) {
            auto func = dyn_cast<func::FuncOp>(op);
            if (!func || func.empty() || func.isExternal()) continue;
            Block& entry = func.front();
            for (Block& block : func.getBlocks()) {
                bool is_entry = (&block == &entry);
                if (!is_entry) {
                    for (Value arg : block.getArguments())
                        try_drop(arg, &block, b, drop_fn,
                                 /*from_call=*/true);
                }
                for (Operation& o : block) {
                    if (o.hasTrait<OpTrait::IsTerminator>()) continue;
                    bool from_call = isa<func::CallOp>(o);
                    for (Value r : o.getResults())
                        try_drop(r, &block, b, drop_fn, from_call);
                }
            }
        }
    }
};

} // namespace

std::unique_ptr<mlir::Pass> createInsertRuntimeDropsPass() {
    return std::make_unique<InsertRuntimeDropsPass>();
}

} // namespace nv
