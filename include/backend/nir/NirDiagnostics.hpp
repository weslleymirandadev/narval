#pragma once

namespace nv {

// Compiler diagnostics that can be asked for on the command line. Plain globals
// rather than an options struct threaded through codegen and the pass pipeline: a
// process compiles one program, and every consumer already sits behind an env-var
// check (NARVAL_VERBOSE / NARVAL_DUMP_NIR / NARVAL_DROPS_DEBUG) that the flags turn
// on too, so the two front doors share one switch.

// --emit-nir: print the narval-dialect module as codegen left it, before any pass
// rewrites it (the only form that still reads like the source).
inline bool& diag_emit_nir() { static bool on = false; return on; }

// --dump-passes: print the module after every pass of the pipeline.
inline bool& diag_dump_passes() { static bool on = false; return on; }

// --explain-ownership: per value, say whether nv_drop was inserted and, when it was
// not, which use blocked it (return, branch operand, container store, user callee).
inline bool& diag_explain_ownership() { static bool on = false; return on; }

} // namespace nv
