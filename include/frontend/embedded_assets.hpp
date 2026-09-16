// The assets a Narval release carries inside the compiler.
//
// A release is ONE file per platform: the runtime objects the linker needs (runtime.o and,
// on POSIX, runtime_nostd.o) and the standard library sources are byte arrays in the narval
// executable, materialised into a per-user home directory on first use
// (src/frontend/narval_home.cpp). Nothing here needs a build tree, NARVAL_HOME or a stdlib/
// directory next to the binary — the on-disk copies are the pre-release development path.
//
// The definitions live in the generated build/generated/narval_runtime_blobs.cpp, produced by
// cmake/NarvalEmbedBlob.cmake. This header is included by that generated file too, so the
// `extern` declarations here are what give the definitions external linkage.
#pragma once

#include <cstddef>
#include <string>

namespace narval {

// One embedded standard library module: the file's name (as it is written to disk) and its
// bytes. `data` is not NUL-terminated — the length is the truth.
struct EmbeddedFile {
    const char*          name;
    const unsigned char* data;
    unsigned int         len;
};

}  // namespace narval

// Runtime objects (empty — len 0 — where the platform has none, e.g. the no_std runtime on
// Windows: it is POSIX-syscall based and the compiler refuses @[no_std] there).
extern unsigned char narval_runtime_obj[];
extern unsigned int  narval_runtime_obj_len;
extern unsigned char narval_runtime_nostd_obj[];
extern unsigned int  narval_runtime_nostd_obj_len;

// The shared runtime library the REPL loads to resolve the nv_* symbols. Windows only: there
// nothing of the runtime is linked into narval.exe, while on POSIX those symbols already live in
// the compiler process and no library is needed (len 0).
extern unsigned char narval_runtime_dll[];
extern unsigned int  narval_runtime_dll_len;

// The embedded standard library, e.g. { "file.nv", <bytes>, 4096 }.
extern const narval::EmbeddedFile narval_stdlib_files[];
extern const unsigned int        narval_stdlib_files_count;

namespace narval {

// Writes the embedded assets into the home directory (see below), exporting NARVAL_HOME and —
// when the caller has not set it — NARVAL_STDLIB, and returns the directory. Returns "" when
// no candidate directory could be written: the compiler then falls back to the build tree /
// NARVAL_HOME / install directory it always used, so a read-only filesystem degrades instead
// of failing.
std::string ensure_embedded_assets();

// The home directory, without writing anything: $NARVAL_HOME when set, otherwise the per-user
// cache directory for this version (~/.cache/narval/<version>, %LOCALAPPDATA%\Narval\<version>).
std::string narval_home_dir();

}  // namespace narval
