#pragma once
#include <string>

namespace nv {
class Checker;

// `comptime import_c("header.h"[, link: "lib"])` — parse a declaration-only C
// header and register its prototypes so they become callable from Narval.
//
// No clang dependency: the header is scanned with a minimal declaration parser
// (comments and preprocessor lines stripped, one prototype per ';' chunk).
// Supported types: void, integer-ish (int/long/short/size_t/...), float/double
// and `char*` strings; anything else (other pointers, structs, varargs) is
// skipped. Each accepted prototype is recorded as a signature shape in
// Checker::c_import_sigs so codegen can route the call through the runtime's
// generic dlsym bridge.
//
// Returns false and fills `error` when the header cannot be read/parsed.
bool import_c_header(Checker* checker, const std::string& path,
                     const std::string& link, std::string& error);

} // namespace nv
