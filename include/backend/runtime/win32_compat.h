// win32_compat.h — the two POSIX pieces the runtime needs on Windows.
//
// The runtime resolves symbols by NAME at run time: closures look up __closure_fn_N and the
// `comptime import_c` bridges look up the C function they were handed. On POSIX that is
// dlsym(RTLD_DEFAULT, name), which walks every loaded object. On Windows it is
// GetProcAddress over the process' export tables, and that works because the program is
// linked with --export-all-symbols (see the link step in src/main.cpp): the generated
// functions are exported by the executable itself, and the CRT functions live in
// ucrtbase/msvcrt.
//
// sysconf(_SC_NPROCESSORS_ONLN) has no Windows equivalent in <unistd.h>; nv_cpu_count()
// is the same fact read from GetSystemInfo.
//
// Usable from both MinGW and MSVC: windows.h only, no POSIX headers.
#ifndef NV_WIN32_COMPAT_H
#define NV_WIN32_COMPAT_H

#ifdef _WIN32

// windows.h defines min/max as macros and that breaks std::min/std::max in any C++ file
// that includes this header.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#define RTLD_LAZY    1
#define RTLD_NOW     2
#define RTLD_GLOBAL  0x100
#define RTLD_LOCAL   0
#define RTLD_DEFAULT ((void*)0)

// A plain LoadLibrary, but a POSIX caller names the file ("sqlite3", "runtime.so"), so a
// failed first attempt is retried with the .dll suffix appended.
static inline void* nv_win_dlopen(const char* path) {
    if (!path) return NULL;
    HMODULE lib = LoadLibraryA(path);
    if (!lib) {
        char buf[MAX_PATH];
        size_t n = strlen(path);
        if (n + 4 < sizeof(buf)) {
            memcpy(buf, path, n);
            memcpy(buf + n, ".dll", 5);
            lib = LoadLibraryA(buf);
        }
    }
    return (void*)lib;
}

static inline void* nv_win_dlsym(void* handle, const char* name) {
    if (!name) return NULL;
    if (handle) return (void*)GetProcAddress((HMODULE)handle, name);

    // RTLD_DEFAULT: the executable first (that is where --export-all-symbols put the
    // generated code), then the CRTs that host atof/pow/malloc.
    HMODULE self = GetModuleHandleA(NULL);
    void* found = self ? (void*)GetProcAddress(self, name) : NULL;
    static const char* const crts[] = {"ucrtbase.dll", "msvcrt.dll"};
    for (size_t i = 0; !found && i < sizeof(crts) / sizeof(crts[0]); i++) {
        HMODULE crt = GetModuleHandleA(crts[i]);
        if (!crt) crt = LoadLibraryA(crts[i]);
        if (crt) found = (void*)GetProcAddress(crt, name);
    }
    return found;
}

static inline int nv_win_dlclose(void* handle) {
    return handle && FreeLibrary((HMODULE)handle) ? 0 : -1;
}

// POSIX dlerror() reports why the last dlopen/dlsym failed; the caller only uses it for a
// message, so the Win32 error code is enough.
static inline const char* nv_win_dlerror(void) {
    static char buf[64];
    DWORD err = GetLastError();
    if (!err) return "symbol not found";
    snprintf(buf, sizeof(buf), "Windows error %lu", (unsigned long)err);
    return buf;
}

#define dlopen(path, flags)   nv_win_dlopen(path)
#define dlsym(handle, name)   nv_win_dlsym(handle, name)
#define dlclose(handle)       nv_win_dlclose(handle)
#define dlerror()             nv_win_dlerror()

// sysconf(_SC_NPROCESSORS_ONLN): how many cpus the parallel loop may spread over.
static inline long nv_cpu_count(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors > 0 ? (long)info.dwNumberOfProcessors : 1;
}

#endif /* _WIN32 */

#endif /* NV_WIN32_COMPAT_H */
