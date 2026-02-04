#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>

class Program;

namespace nv {
class Checker;
}

namespace llvm {
class Module;
}

namespace llvm::orc {
class ThreadSafeContext;
}

namespace narval::frontend::interactive {

// IR Incremental Builder
//
// Model:
// - Each interactive execution produces an IR "fragment" (one LLVM module with
//   a single entry function).
// - All fragments are added to the same JITDylib, so LLVM symbols are resolved
//   incrementally through normal external linkage.
// - We track fragment dependencies at the *symbol interface* level (defined vs.
//   used symbols). This allows us to invalidate dependents transitively when a
//   fragment is rebuilt.
//
// Design constraints:
// - This component is independent from SessionManager.
// - It exposes clear integration points with the JIT by returning a module
//   + entry function name, and by providing invalidation sets that the JIT layer
//   can unload.

struct IrBuildOptions {
    bool auto_print_last_expr = true;
};

struct IrBuildResult {
    bool ok = false;
    std::string error;

    std::string entry_function;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::orc::ThreadSafeContext> tsc;
};

struct IrFragment {
    std::string id;
    bool active = true;

    std::string unit_name;
    std::string entry_function;

    std::unordered_set<std::string> defined_symbols;
    std::unordered_set<std::string> used_symbols;
};

struct IrInvalidateResult {
    std::vector<std::string> invalidated_fragments;
    std::unordered_set<std::string> affected_symbols;
};

struct IrRebuildResult {
    IrBuildResult build;
    IrInvalidateResult invalidation;
};

class IrIncrementalBuilder {
public:
    IrIncrementalBuilder();

    void reset();

    // Build (or rebuild) a fragment.
    //
    // - fragment_id: stable logical identifier (e.g. repl step id or cell id).
    // - unit_name: unique name for LLVM module/function symbol namespace.
    //             Callers usually pass a monotonic counter.
    //
    // The caller is expected to call commit_fragment_interface() after a
    // successful build (usually after semantic analysis), so dependency edges
    // can be updated.
    IrBuildResult build_fragment(
        Program& program,
        nv::Checker& checker,
        const std::string& fragment_id,
        const std::string& unit_name,
        const IrBuildOptions& options
    );

    // Convenience API: invalidate_fragment(fragment_id) and then build_fragment().
    // Note: this does not rebuild dependents automatically; it returns the list
    // of fragments that should be rebuilt by higher-level orchestration.
    IrRebuildResult rebuild_fragment(
        Program& program,
        nv::Checker& checker,
        const std::string& fragment_id,
        const std::string& unit_name,
        const IrBuildOptions& options
    );

    // Commit symbol interface for a fragment and update dependency graph.
    void commit_fragment_interface(
        const std::string& fragment_id,
        const std::unordered_set<std::string>& defined_symbols,
        const std::unordered_set<std::string>& used_symbols
    );

    // Invalidate a fragment and all fragments that depend on it (transitively).
    // Returns the invalidated fragment ids, plus the set of symbols that become
    // unavailable.
    IrInvalidateResult invalidate_fragment(const std::string& fragment_id);

    bool is_fragment_active(const std::string& fragment_id) const;
    const IrFragment* get_fragment(const std::string& fragment_id) const;

private:
    static bool last_stmt_is_write_call(const Program& program);
    static bool last_stmt_can_autoprint(const Program& program);

    void set_fragment_dependencies(const std::string& fragment_id, const std::unordered_set<std::string>& deps);
    std::vector<std::string> bfs_collect_dependents(const std::string& fragment_id) const;

    std::unordered_map<std::string, IrFragment> fragments_;
    std::unordered_map<std::string, std::unordered_set<std::string>> deps_;
    std::unordered_map<std::string, std::unordered_set<std::string>> rdeps_;
    std::unordered_map<std::string, std::string> symbol_producer_fragment_;
    
    // Map to store global variable addresses for cross-module symbol resolution
    std::unordered_map<std::string, llvm::GlobalVariable*> global_variables_;
    // Map to store actual global variable values from previous fragments
    std::unordered_map<std::string, llvm::Constant*> global_initializers_;
};

} // namespace narval::frontend::interactive
