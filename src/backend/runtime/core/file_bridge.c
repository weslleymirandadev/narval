// File I/O for Narval programs.
//
// Handles are small integers: Narval integers are 32-bit, so a FILE* would not fit.
// Text comes back as `const char*` pointing at one shared buffer that the next read
// overwrites — the Narval-facing wrappers copy it into a string immediately, so a
// program never holds a pointer into it.
//
// Modes are the usual C ones ("r", "w", "a", plus "rb"/"wb" and combinations).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "backend/runtime/nv_runtime.h"

#define NV_FILE_MAX 32

static FILE* g_files[NV_FILE_MAX];
static char* g_buf = NULL;
static size_t g_buf_cap = 0;

static int nv_file_slot(int handle) {
    return (handle >= 0 && handle < NV_FILE_MAX && g_files[handle]) ? handle : -1;
}

static char* nv_file_reserve(size_t need) {
    if (g_buf_cap >= need) return g_buf;
    size_t next = g_buf_cap ? g_buf_cap : 256;
    while (next < need) next *= 2;
    char* grown = (char*)realloc(g_buf, next);
    if (!grown) return NULL;
    g_buf = grown;
    g_buf_cap = next;
    return g_buf;
}

// Opens a file; returns a handle or -1.
int nv_file_open(const char* path, const char* mode) {
    if (!path || !mode) return -1;
    for (int i = 0; i < NV_FILE_MAX; ++i) {
        if (g_files[i]) continue;
        FILE* f = fopen(path, mode);
        if (!f) return -1;
        g_files[i] = f;
        return i;
    }
    return -1;
}

// Closes a handle; 0 on success.
int nv_file_close(int handle) {
    if (nv_file_slot(handle) < 0) return -1;
    fclose(g_files[handle]);
    g_files[handle] = NULL;
    return 0;
}

// Whole remaining content of the file ("" at end of file or on a bad handle).
const char* nv_file_read(int handle) {
    if (nv_file_slot(handle) < 0) return "";
    FILE* f = g_files[handle];
    size_t used = 0;
    size_t cap = g_buf_cap;
    if (!nv_file_reserve(4096)) return "";
    cap = g_buf_cap;
    g_buf[0] = '\0';
    size_t n;
    while ((n = fread(g_buf + used, 1, cap - used - 1, f)) > 0) {
        used += n;
        if (used + 1 >= cap) {
            if (!nv_file_reserve(cap * 2)) break;
            cap = g_buf_cap;
        }
    }
    g_buf[used] = '\0';
    return g_buf;
}

// One line, without the trailing newline ("" at end of file).
const char* nv_file_read_line(int handle) {
    if (nv_file_slot(handle) < 0) return "";
    FILE* f = g_files[handle];
    if (!nv_file_reserve(1024)) return "";
    size_t used = 0;
    size_t cap = g_buf_cap;
    int c;
    while ((c = fgetc(f)) != EOF && c != '\n') {
        if (used + 2 > cap) {
            if (!nv_file_reserve(cap * 2)) break;
            cap = g_buf_cap;
        }
        g_buf[used++] = (char)c;
        if (c == '\r') --used;  // tolerate CRLF
    }
    g_buf[used] = '\0';
    return g_buf;
}

// Writes text; the number of characters written, or -1.
int nv_file_write(int handle, const char* text) {
    if (nv_file_slot(handle) < 0 || !text) return -1;
    const size_t len = strlen(text);
    const size_t written = fwrite(text, 1, len, g_files[handle]);
    fflush(g_files[handle]);
    return (int)written;
}

// 1 when the path exists.
int nv_file_exists(const char* path) {
    if (!path) return 0;
    struct stat st;
    return stat(path, &st) == 0 ? 1 : 0;
}

// Removes a file; 0 on success.
int nv_file_remove(const char* path) {
    if (!path) return -1;
    return remove(path) == 0 ? 0 : -1;
}

// ── Narval-facing wrappers ────────────────────────────────────────────────

static const char* nv_file_arg_str(NvObject* o) {
    return (o && o->ob_type == NVStr_Type) ? ((NVStr*)o)->value : NULL;
}

static int32_t nv_file_arg_int(NvObject* o) {
    return (o && o->ob_type == NVInt_Type) ? ((NVInt*)o)->value : -1;
}

static NvObject* nv_file_box_i(int v) {
    Value out = {NULL};
    create_int(&out, v);
    return out.obj;
}

static NvObject* nv_file_box_s(const char* s) {
    Value out = {NULL};
    create_str(&out, s ? s : "");
    return out.obj;
}

NvObject* nv_file_open_builtin(NvObject* path, NvObject* mode) {
    return nv_file_box_i(nv_file_open(nv_file_arg_str(path), nv_file_arg_str(mode)));
}

NvObject* nv_file_close_builtin(NvObject* handle) {
    return nv_file_box_i(nv_file_close(nv_file_arg_int(handle)));
}

NvObject* nv_file_read_builtin(NvObject* handle) {
    return nv_file_box_s(nv_file_read(nv_file_arg_int(handle)));
}

NvObject* nv_file_read_line_builtin(NvObject* handle) {
    return nv_file_box_s(nv_file_read_line(nv_file_arg_int(handle)));
}

NvObject* nv_file_write_builtin(NvObject* handle, NvObject* text) {
    return nv_file_box_i(nv_file_write(nv_file_arg_int(handle), nv_file_arg_str(text)));
}

NvObject* nv_file_exists_builtin(NvObject* path) {
    return nv_file_box_i(nv_file_exists(nv_file_arg_str(path)));
}

NvObject* nv_file_remove_builtin(NvObject* path) {
    return nv_file_box_i(nv_file_remove(nv_file_arg_str(path)));
}
