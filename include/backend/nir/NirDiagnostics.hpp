#pragma once

#include <set>
#include <string>

namespace nv {

// Compiler diagnostics that can be asked for on the command line. Plain globals
// rather than an options struct threaded through codegen and the pass pipeline: a
// process compiles one program, and every consumer already sits behind an env-var
// check (NARVAL_VERBOSE / NARVAL_DUMP_NIR / NARVAL_DROPS_DEBUG) that the flags turn
// on too, so the two front doors share one switch.

// --emit-nir: print the narval-dialect module as codegen left it, before any pass
// rewrites it (the only form that still reads like the source).
inline bool& diag_emit_nir() { static bool on = false; return on; }


inline bool& diag_emit_llvm() { static bool on = false; return on; }

// --dump-passes: print the module after every pass of the pipeline.
inline bool& diag_dump_passes() { static bool on = false; return on; }

// How much of the module the ownership report covers. The report is about the code
// the programmer wrote, and every program carries the stdlib prelude, so "the whole
// module" would drown the five lines of interest in the prelude's functions.
enum class OwnershipReport {
    Off = 0,      // no report
    UserFile = 1, // --explain-ownership: only the file being compiled
    AllFiles = 2  // --explain-ownership=all: the stdlib/runtime functions too
};

// --explain-ownership[=all]: the source-level ownership report (events anchored in
// the .nv file: own/borrow/move/mut/share/drop/keep), not the raw per-IR-value trace.
inline OwnershipReport& diag_ownership_report() {
    static OwnershipReport mode = OwnershipReport::Off;
    return mode;
}
inline bool diag_explain_ownership() { return diag_ownership_report() != OwnershipReport::Off; }

// --explain-ownership=ir: append the IR line behind each event (the value, the
// bridge that produced it, how many uses it has) for debugging the pass itself.
inline bool& diag_ownership_ir() { static bool on = false; return on; }

// NARVAL_DUMP_LOCS=1: print source locations in --emit-nir/--dump-passes. The MLIR
// printer hides them unless debug info is enabled, which reads as "the IR carries no
// locations at all" while every op actually has the FileLineColLoc the codegen gave
// it — and that is what the ownership report is anchored on.
inline bool& diag_dump_locs() { static bool on = false; return on; }

// Functions and classes the combined program took from files OTHER than the one being
// compiled (the stdlib prelude, imported modules). The frontend knows this (it is the
// module merge that copies them in) and the ownership report needs it: every program
// carries the prelude, and reporting its functions would bury the program's own code.
inline std::set<std::string>& diag_prelude_functions() {
    static std::set<std::string> names;
    return names;
}
inline std::set<std::string>& diag_prelude_classes() {
    static std::set<std::string> names;
    return names;
}

} // namespace nv
