// InsertRuntimeDropsPass: frees runtime heap temporaries.
//
// Every runtime call (nv_box_*, nv_add, nv_index_to_value, ...) returns a
// freshly allocated NvObject* with ref_count == 1, and until now nothing ever
// dropped them: a loop body allocating a few boxes per iteration grew memory
// linearly (1M iterations ≈ 64 MB, 10M ≈ 626 MB — measured).
//
// The runtime has full refcounting (nv_incref/nv_decref + tp_dealloc) but the
// compiled code never calls it. This pass runs AFTER lower-narval-to-std
// (values are !llvm.ptr by then) and inserts `call @nv_drop(%v)` after every
// LAST USE of a pointer value, where "last use" is decided by reachability over
// the CFG: a use is last when no other use is reachable from it, so the drop can
// never free the object while a later use still needs it. Uses on mutually
// exclusive paths are each last and each gets its own drop (only one runs).
// Reachability follows back edges, so a use inside a loop that can reach itself
// is never last — loop-carried objects stay untouched.
//
// Ownership is never transferred by a use that is a branch operand: those
// become the destination block's args, so the destination owns the object and
// drops it when IT dies (non-entry block args are drop candidates). A value
// whose use escapes through func.return, or that a callee may store (container
// push/set, user function), is left alone — it can then leak, never double-free.
//
// The function-entry block args are intentionally not dropped: the caller
// owns the objects it passes (and drops them after the call).

#include "backend/nir/NirDiagnostics.hpp"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"

#include <functional>
#include <string>

using namespace mlir;

namespace nv {
namespace {

// Every use of `v` reachable from `from` (walking the CFG from the block that
// contains it). Uses later in the SAME block always count, whatever the block is —
// missing that made the first of two uses in one block look like the last one and
// freed the object while the second use still read it. Successors are followed
// through whatever BranchOpInterface terminator the block has. Re-entering the
// definition block is NOT followed: that is a loop back edge, which produces a NEW
// object, so uses past it belong to the next iteration — without this a use inside
// a loop could reach itself and no loop-local temporary would ever be freed.
static bool other_use_reachable(Operation* from,
                                ArrayRef<Operation*> uses,
                                Block* def_block) {
    Block* from_block = from->getBlock();
    for (Operation* w : uses)
        if (w != from && w->getBlock() == from_block &&
            from->isBeforeInBlock(w))
            return true;

    llvm::SmallPtrSet<Block*, 8> seen;
    SmallVector<Block*, 8> worklist;
    bool loops_back = false;
    auto push_succs = [&](Block* blk) {
        if (!blk || blk->empty()) return;
        if (auto br = dyn_cast<BranchOpInterface>(blk->back())) {
            for (unsigned i = 0; i < br->getNumSuccessors(); ++i) {
                Block* s = br->getSuccessor(i);
                if (s == def_block) continue;
                if (s == from_block) { loops_back = true; continue; }
                if (seen.insert(s).second) worklist.push_back(s);
            }
        }
    };
    seen.insert(from_block);
    push_succs(from_block);

    while (!worklist.empty()) {
        Block* blk = worklist.pop_back_val();
        for (Operation* w : uses)
            if (w != from && w->getBlock() == blk) return true;
        push_succs(blk);
    }

    // The block reaches itself again without passing through the definition, so the
    // object is not re-created on the way: this very use runs again on the next
    // iteration and the value is still live after it. (When the definition IS inside
    // the cycle it re-executes instead, which makes a new object and leaves this use
    // free to be the last one.)
    if (loops_back) return true;

    return false;
}

// The op that has to finish before `v` may die. `nv_container_get` hands back a
// pointer INTO the container, and dropping the container right after that read freed
// the value under the call that was about to consume it (`write(m["k"])` read freed
// memory). So the drop goes after the LAST op in the block that depends on the use —
// the read, the write that takes the read as an argument, anything chained further.
static Operation* last_consumer(Operation* u) {
    Block* block = u->getBlock();
    Operation* sink = u;
    bool moved = true;
    while (moved) {
        moved = false;
        for (Operation* user : sink->getUsers()) {
            if (user->getBlock() != block) continue;
            if (user->hasTrait<OpTrait::IsTerminator>()) continue;
            if (user->isBeforeInBlock(sink)) continue;  // keep the walk forward
            sink = user;
            moved = true;
        }
    }
    return sink;
}

// Label for a value's producer, for the NARVAL_DROPS_DEBUG trace.
static std::string producer_label(Value v) {
    Operation* op = v.getDefiningOp();
    if (!op) return "<block-arg>";
    if (auto c = dyn_cast<func::CallOp>(op)) return c.getCallee().str();
    return op->getName().getStringRef().str();
}

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
    // nv_len...) are pointers INTO containers and must not be dropped. Anything
    // added here must really allocate: dropping an object that someone else
    // still references frees it under them.
    static bool owned_ret(const std::string& name) {
        if (name.rfind("nv_box_", 0) == 0) return true;
        if (name.rfind("nv_create_", 0) == 0) return true;
        if (name == "nv_index_to_value") return true;
        if (name == "nv_make_none") return true;
        static const char* const fresh[] = {
            // arithmetic produces a new object
            "nv_add", "nv_sub", "nv_mul", "nv_div", "nv_mod",
            // conversions box a new value
            "nv_int_builtin", "nv_float_builtin", "nv_str_builtin",
            "nv_bool_builtin", "nv_char_builtin", "nv_len_builtin",
            // comparisons box a new bool
            "nv_value_lt", "nv_value_gt", "nv_value_le", "nv_value_ge",
            "nv_value_eq", "nv_value_ne",
            // option/result wrappers
            "nv_make_some", "nv_make_ok", "nv_make_err",
            // helpers that return their own box (json_field increfs the
            // fallback it returns, so the drop balances it)
            "nv_json_field_builtin", "nv_read_builtin",
        };
        for (const char* a : fresh)
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

    // Returns the number of nv_drop calls inserted.
    static int try_drop(Value v, Block* def_block, OpBuilder& b,
                        func::FuncOp drop_fn, bool from_call, bool debug_on) {
        if (!isa<LLVM::LLVMPointerType>(v.getType())) return 0;
        // Only drop heap NvObject*. Data pointers (string-literal addresses,
        // memref bases) are !llvm.ptr too but are NOT runtime objects — they
        // come from addressof/getelementptr ops, never from func.call. And
        // only drop results of owning calls (see owned_ret). Block args are
        // carried runtime values and are always owned.
        if (!from_call) return 0;
        Operation* prod = v.getDefiningOp();
        if (prod) {
            auto call = dyn_cast<func::CallOp>(prod);
            if (!call) return 0;
            if (!owned_ret(call.getCallee().str())) {
                if (debug_on)
                    llvm::errs() << "[ownership] keep " << call.getCallee()
                                 << " result: it is a view into another object, "
                                    "not a fresh one\n";
                return 0;
            }
        }

        // Uses that keep the value alive elsewhere make it a non-candidate:
        // escaping through func.return, carried by a branch (the destination
        // block arg owns it), or stored by a callee that takes ownership.
        SmallVector<Operation*, 4> uses;
        bool flows = false, stored = false;
        std::string stored_by;
        for (Operation* user : v.getUsers()) {
            if (isa<func::ReturnOp>(user)) {
                if (debug_on)
                    llvm::errs() << "[ownership] keep " << producer_label(v)
                                 << ": it is returned (the caller owns it now)\n";
                return 0;  // escapes as the result
            }
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
                if (guards_args(cname) || cname.rfind("nv_", 0) != 0) {
                    stored = true;
                    if (stored_by.empty()) stored_by = cname;
                    continue;
                }
            }
            uses.push_back(user);
        }
        if (flows || stored) {
            if (debug_on) {
                llvm::errs() << "[ownership] keep " << producer_label(v) << ": ";
                if (stored)
                    llvm::errs() << stored_by
                                 << " may keep it (no incref across the call)";
                if (stored && flows) llvm::errs() << ", and it is ";
                if (flows)
                    llvm::errs() << "carried by a branch (the destination block "
                                    "owns it)";
                llvm::errs() << "\n";
            }
            return 0;
        }

        // Unused value: nothing can reference it, so it dies at its definition
        // (a block arg dies at the top of its block). Without this an unused
        // temporary — `n = len(s)` where n is never read — leaks.
        if (uses.empty()) {
            Operation* def = v.getDefiningOp();
            Location loc = def ? def->getLoc() : b.getUnknownLoc();
            if (def)            b.setInsertionPointAfter(def);
            else if (!def_block->empty()) b.setInsertionPointToStart(def_block);
            else                return 0;
            b.create<func::CallOp>(loc, drop_fn, ValueRange{v});
            if (debug_on)
                llvm::errs() << "[ownership] drop " << producer_label(v)
                             << " at its definition: no use at all\n";
            return 1;
        }

        // Drop after each use nothing else can reach. Uses on mutually exclusive
        // paths are each last and each gets its own drop; exactly one of them
        // executes.
        int inserted = 0;
        for (Operation* u : uses) {
            if (other_use_reachable(u, uses, def_block)) continue;
            Operation* sink = last_consumer(u);
            b.setInsertionPointAfter(sink);
            b.create<func::CallOp>(sink->getLoc(), drop_fn, ValueRange{v});
            ++inserted;
        }
        if (debug_on)
            llvm::errs() << "[ownership] " << (inserted ? "drop" : "keep") << " "
                         << producer_label(v) << " after "
                         << (inserted ? "its last use" : "no use is last")
                         << " (" << uses.size() << " use"
                         << (uses.size() == 1 ? "" : "s")
                         << (inserted ? "" : ", every use can reach another")
                         << ")\n";
        return inserted;
    }

    void runOnOperation() override {
        ModuleOp module = getOperation();
        OpBuilder b(module.getContext());
        func::FuncOp drop_fn = get_or_create_drop_fn(module, b);
        const bool debug_on = nv::diag_explain_ownership() ||
                              std::getenv("NARVAL_DROPS_DEBUG") != nullptr;
        int total = 0;

        // Every block of the function, regions included: a result-carrying scf.if
        // keeps its body in a region, and those blocks are NOT in the function's own
        // block list, so a temporary produced inside one was dropped by nobody.
        std::function<void(Block&, bool)> visit = [&](Block& block, bool is_entry) {
            if (!is_entry) {
                for (Value arg : block.getArguments())
                    total += try_drop(arg, &block, b, drop_fn,
                                      /*from_call=*/true, debug_on);
            }
            for (Operation& o : block) {
                if (!o.hasTrait<OpTrait::IsTerminator>()) {
                    bool from_call = isa<func::CallOp>(o);
                    for (Value r : o.getResults())
                        total += try_drop(r, &block, b, drop_fn, from_call,
                                          debug_on);
                }
                for (Region& region : o.getRegions())
                    for (Block& inner : region)
                        visit(inner, /*is_entry=*/false);
            }
        };

        for (Operation& op : module.getBody()->getOperations()) {
            auto func = dyn_cast<func::FuncOp>(op);
            if (!func || func.empty() || func.isExternal()) continue;
            if (debug_on)
                llvm::errs() << "[ownership] func @" << func.getName() << "\n";
            Block& entry = func.front();
            for (Block& block : func.getBlocks())
                visit(block, &block == &entry);
        }

        if (debug_on)
            llvm::errs() << "[ownership] " << total
                         << " nv_drop call(s) inserted in total\n";
    }
};

} // namespace

std::unique_ptr<mlir::Pass> createInsertRuntimeDropsPass() {
    return std::make_unique<InsertRuntimeDropsPass>();
}

} // namespace nv
