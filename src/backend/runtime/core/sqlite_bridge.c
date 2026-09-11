// SQLite access for Narval programs, exposed to the standard library through
// import_c. Plain C ABI on purpose:
//   - a handle is a small integer (Narval integers are 32-bit, a pointer is not
//     representable);
//   - text comes back as `const char*`, pointing at a per-handle buffer that the
//     next call on the same handle overwrites.
// libsqlite3 is opened on first use with dlopen, so a program that never touches
// SQLite keeps building and running without linking the library.
//
// A query result is text: values separated by '\t', rows by '\n', with tabs,
// newlines and backslashes escaped as \t, \n and \\ so a value cannot break the
// shape. stdlib/sqlite.nv splits that back into rows.

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/runtime/nv_runtime.h"

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

#define NV_SQLITE_MAX_DB 16
#define NV_SQLITE_OK 0
#define NV_SQLITE_ROW 100

typedef struct {
    sqlite3* db;
    char* scratch;  // last text result, reused by the next call and freed on close
    size_t scratch_cap;
    char** cells;   // materialised result set: rows * cols strings
    int rows;
    int cols;
    char** col_names;  // column names of that result set (cols entries) or NULL
} NvSqliteSlot;

static void nv_sqlite_clear_cells(NvSqliteSlot* slot);  // defined below

static NvSqliteSlot g_slots[NV_SQLITE_MAX_DB];
static int g_loaded = 0;
static int g_last = -1;
static char g_load_error[256];

static int (*p_open)(const char*, sqlite3**);
static int (*p_close)(sqlite3*);
static int (*p_exec)(sqlite3*, const char*, void*, void*, char**);
static const char* (*p_errmsg)(sqlite3*);
static int (*p_prepare)(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
static int (*p_step)(sqlite3_stmt*);
static int (*p_finalize)(sqlite3_stmt*);
static int (*p_col_count)(sqlite3_stmt*);
static const unsigned char* (*p_col_text)(sqlite3_stmt*, int);
static const char* (*p_col_name)(sqlite3_stmt*, int);
static void (*p_free)(void*);
static int64_t (*p_last_id)(sqlite3*);
static int (*p_changes)(sqlite3*);

static int nv_sqlite_load(void) {
    if (g_loaded) return g_load_error[0] == '\0';
    g_loaded = 1;
    void* lib = dlopen("libsqlite3.so.0", RTLD_NOW);
    if (!lib) lib = dlopen("libsqlite3.so", RTLD_NOW);
    if (!lib) {
        snprintf(g_load_error, sizeof(g_load_error), "cannot load libsqlite3: %s", dlerror());
        return 0;
    }
    p_open      = (int (*)(const char*, sqlite3**))dlsym(lib, "sqlite3_open");
    p_close     = (int (*)(sqlite3*))dlsym(lib, "sqlite3_close");
    p_exec      = (int (*)(sqlite3*, const char*, void*, void*, char**))dlsym(lib, "sqlite3_exec");
    p_errmsg    = (const char* (*)(sqlite3*))dlsym(lib, "sqlite3_errmsg");
    p_prepare   = (int (*)(sqlite3*, const char*, int, sqlite3_stmt**, const char**))dlsym(lib, "sqlite3_prepare_v2");
    p_step      = (int (*)(sqlite3_stmt*))dlsym(lib, "sqlite3_step");
    p_finalize  = (int (*)(sqlite3_stmt*))dlsym(lib, "sqlite3_finalize");
    p_col_count = (int (*)(sqlite3_stmt*))dlsym(lib, "sqlite3_column_count");
    p_col_text  = (const unsigned char* (*)(sqlite3_stmt*, int))dlsym(lib, "sqlite3_column_text");
    p_col_name  = (const char* (*)(sqlite3_stmt*, int))dlsym(lib, "sqlite3_column_name");
    p_free      = (void (*)(void*))dlsym(lib, "sqlite3_free");
    p_last_id   = (int64_t (*)(sqlite3*))dlsym(lib, "sqlite3_last_insert_rowid");
    p_changes   = (int (*)(sqlite3*))dlsym(lib, "sqlite3_changes");
    if (!p_open || !p_close || !p_exec || !p_errmsg || !p_prepare || !p_step ||
        !p_finalize || !p_col_count || !p_col_text || !p_col_name || !p_free ||
        !p_last_id || !p_changes) {
        snprintf(g_load_error, sizeof(g_load_error), "libsqlite3 is missing an expected symbol");
        return 0;
    }
    return 1;
}

static sqlite3* nv_sqlite_db(int handle) {
    if (handle < 0 || handle >= NV_SQLITE_MAX_DB) return NULL;
    return g_slots[handle].db;
}

// Appends a single raw character (used for the row and cell separators, which
// must not be escaped).
static void nv_sqlite_append_raw(NvSqliteSlot* slot, char c, size_t* used, size_t* cap) {
    if (*used + 2 > *cap) {
        size_t next = *cap ? *cap * 2 : 256;
        while (next < *used + 2) next *= 2;
        char* grown = (char*)realloc(slot->scratch, next);
        if (!grown) return;
        slot->scratch = grown;
        *cap = next;
    }
    slot->scratch[(*used)++] = c;
}

// Appends `s` to the slot buffer, escaping the separators.
static void nv_sqlite_append(NvSqliteSlot* slot, const char* s, size_t* used, size_t* cap) {
    for (const char* p = s; p && *p; ++p) {
        const char* esc = NULL;
        if (*p == '\t') esc = "\\t";
        else if (*p == '\n') esc = "\\n";
        else if (*p == '\\') esc = "\\\\";
        const size_t need = esc ? 2 : 1;
        if (*used + need + 1 > *cap) {
            size_t next = *cap ? *cap * 2 : 256;
            while (next < *used + need + 1) next *= 2;
            char* grown = (char*)realloc(slot->scratch, next);
            if (!grown) return;
            slot->scratch = grown;
            *cap = next;
        }
        if (esc) {
            slot->scratch[(*used)++] = esc[0];
            slot->scratch[(*used)++] = esc[1];
        } else {
            slot->scratch[(*used)++] = *p;
        }
    }
}

// Opens a database; returns a handle or -1.
int nv_sqlite_open(const char* path) {
    if (!nv_sqlite_load() || !path) return -1;
    for (int i = 0; i < NV_SQLITE_MAX_DB; ++i) {
        if (g_slots[i].db) continue;
        sqlite3* db = NULL;
        if (p_open(path, &db) != NV_SQLITE_OK || !db) {
            if (db) p_close(db);
            return -1;
        }
        g_slots[i].db = db;
        g_slots[i].scratch = NULL;
        g_slots[i].scratch_cap = 0;
        g_last = i;
        return i;
    }
    return -1;
}

// Closes a database; returns 0 on success.
int nv_sqlite_close(int handle) {
    sqlite3* db = nv_sqlite_db(handle);
    if (!db) return -1;
    p_close(db);
    g_slots[handle].db = NULL;
    free(g_slots[handle].scratch);
    g_slots[handle].scratch = NULL;
    g_slots[handle].scratch_cap = 0;
    nv_sqlite_clear_cells(&g_slots[handle]);
    return 0;
}

// Runs a statement that returns nothing; 0 on success, else the SQLite error code.
int nv_sqlite_exec(int handle, const char* sql) {
    sqlite3* db = nv_sqlite_db(handle);
    if (!db || !sql) return -1;
    char* err = NULL;
    const int rc = p_exec(db, sql, NULL, NULL, &err);
    if (err) p_free(err);  // sqlite allocates it, free() would corrupt the heap
    g_last = handle;
    return rc;
}

// Runs a query; rows as text (values by tab, rows by newline), "" on failure.
const char* nv_sqlite_query(int handle, const char* sql) {
    sqlite3* db = nv_sqlite_db(handle);
    if (!db || !sql) return "";
    NvSqliteSlot* slot = &g_slots[handle];
    g_last = handle;
    size_t used = 0;
    size_t cap = slot->scratch_cap;
    if (slot->scratch && cap) slot->scratch[0] = '\0';

    sqlite3_stmt* stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != NV_SQLITE_OK || !stmt) {
        free(slot->scratch);
        slot->scratch = NULL;
        slot->scratch_cap = 0;
        return "";
    }
    const int cols = p_col_count(stmt);
    int first_row = 1;
    while (p_step(stmt) == NV_SQLITE_ROW) {
        if (!first_row) nv_sqlite_append_raw(slot, '\n', &used, &cap);
        first_row = 0;
        for (int c = 0; c < cols; ++c) {
            if (c) nv_sqlite_append_raw(slot, '\t', &used, &cap);
            const unsigned char* text = p_col_text(stmt, c);
            nv_sqlite_append(slot, text ? (const char*)text : "", &used, &cap);
        }
    }
    p_finalize(stmt);
    if (slot->scratch) slot->scratch[used < cap ? used : (cap ? cap - 1 : 0)] = '\0';
    slot->scratch_cap = cap;
    return slot->scratch ? slot->scratch : "";
}

// Last error message of a handle ("" when there is none).
const char* nv_sqlite_error(int handle) {
    if (!g_loaded) nv_sqlite_load();
    if (g_load_error[0]) return g_load_error;
    sqlite3* db = nv_sqlite_db(handle >= 0 ? handle : g_last);
    if (!db) return "";
    return p_errmsg(db);
}

// Rowid of the last insert on a handle.
int nv_sqlite_last_id(int handle) {
    sqlite3* db = nv_sqlite_db(handle);
    if (!db) return -1;
    return (int)p_last_id(db);
}

// Rows changed by the last statement on a handle.
int nv_sqlite_changes(int handle) {
    sqlite3* db = nv_sqlite_db(handle);
    if (!db) return -1;
    return p_changes(db);
}

// ── Narval-facing wrappers ────────────────────────────────────────────────
// The functions above are plain C (usable from any C code). These box their
// results as runtime values so the language can call them directly; the
// standard library (stdlib/sqlite.nv) wraps them with a nicer API.

static const char* nv_sqlite_arg_str(NvObject* o) {
    return (o && o->ob_type == NVStr_Type) ? ((NVStr*)o)->value : NULL;
}

static int32_t nv_sqlite_arg_int(NvObject* o) {
    return (o && o->ob_type == NVInt_Type) ? ((NVInt*)o)->value : -1;
}

static NvObject* nv_sqlite_box_i(int v) {
    Value out = {NULL};
    create_int(&out, v);
    return out.obj;
}

static NvObject* nv_sqlite_box_s(const char* s) {
    Value out = {NULL};
    create_str(&out, s ? s : "");
    return out.obj;
}

NvObject* nv_sqlite_open_builtin(NvObject* path) {
    return nv_sqlite_box_i(nv_sqlite_open(nv_sqlite_arg_str(path)));
}

NvObject* nv_sqlite_close_builtin(NvObject* handle) {
    return nv_sqlite_box_i(nv_sqlite_close(nv_sqlite_arg_int(handle)));
}

NvObject* nv_sqlite_exec_builtin(NvObject* handle, NvObject* sql) {
    return nv_sqlite_box_i(nv_sqlite_exec(nv_sqlite_arg_int(handle), nv_sqlite_arg_str(sql)));
}

NvObject* nv_sqlite_query_builtin(NvObject* handle, NvObject* sql) {
    return nv_sqlite_box_s(nv_sqlite_query(nv_sqlite_arg_int(handle), nv_sqlite_arg_str(sql)));
}

NvObject* nv_sqlite_error_builtin(NvObject* handle) {
    return nv_sqlite_box_s(nv_sqlite_error(nv_sqlite_arg_int(handle)));
}

NvObject* nv_sqlite_last_id_builtin(NvObject* handle) {
    return nv_sqlite_box_i(nv_sqlite_last_id(nv_sqlite_arg_int(handle)));
}

NvObject* nv_sqlite_changes_builtin(NvObject* handle) {
    return nv_sqlite_box_i(nv_sqlite_changes(nv_sqlite_arg_int(handle)));
}

// ── Materialised result sets ──────────────────────────────────────────────
// nv_sqlite_query_run stores the result set on the handle so each cell can be
// read back as text. The standard library walks rows and cells with plain loops;
// parsing the result inside Narval is not possible yet because an assignment made
// inside an if branch that cannot be if-converted does not escape the branch.

static void nv_sqlite_clear_cells(NvSqliteSlot* slot) {
    if (slot->col_names) {
        for (int i = 0; i < slot->cols; ++i) free(slot->col_names[i]);
        free(slot->col_names);
        slot->col_names = NULL;
    }
    if (!slot->cells) return;
    for (int i = 0; i < slot->rows * slot->cols; ++i) free(slot->cells[i]);
    free(slot->cells);
    slot->cells = NULL;
    slot->rows = 0;
    slot->cols = 0;
}

// Runs a query and stores the result; returns the number of rows (-1 on failure).
int nv_sqlite_query_run(int handle, const char* sql) {
    sqlite3* db = nv_sqlite_db(handle);
    if (!db || !sql) return -1;
    NvSqliteSlot* slot = &g_slots[handle];
    g_last = handle;
    nv_sqlite_clear_cells(slot);

    sqlite3_stmt* stmt = NULL;
    if (p_prepare(db, sql, -1, &stmt, NULL) != NV_SQLITE_OK || !stmt) return -1;
    const int cols = p_col_count(stmt);
    // Column names, captured while the statement is alive: they are what lets a row
    // be read by name (the sql derive maps a row to map<str, str>).
    if (cols > 0) {
        char** names = (char**)calloc((size_t)cols, sizeof(char*));
        if (names) {
            for (int c = 0; c < cols; ++c) {
                const char* n = p_col_name(stmt, c);
                names[c] = strdup(n ? n : "");
            }
            slot->col_names = names;
        }
    }
    int cap = 64;
    int rows = 0;
    char** cells = (char**)calloc((size_t)cap * (size_t)(cols > 0 ? cols : 1), sizeof(char*));
    if (!cells) {
        p_finalize(stmt);
        return -1;
    }
    while (p_step(stmt) == NV_SQLITE_ROW) {
        if (rows == cap) {
            cap *= 2;
            char** grown = (char**)realloc(cells, (size_t)cap * (size_t)(cols > 0 ? cols : 1) * sizeof(char*));
            if (!grown) break;
            cells = grown;
        }
        for (int c = 0; c < cols; ++c) {
            const unsigned char* text = p_col_text(stmt, c);
            cells[rows * cols + c] = strdup(text ? (const char*)text : "");
        }
        rows++;
    }
    p_finalize(stmt);
    slot->cells = cells;
    slot->rows = rows;
    slot->cols = cols;
    return rows;
}

// Columns of the last stored result.
int nv_sqlite_col_count(int handle) {
    if (handle < 0 || handle >= NV_SQLITE_MAX_DB) return 0;
    return g_slots[handle].cols;
}

// Name of a column of the last stored result ("" when out of range).
const char* nv_sqlite_col_name(int handle, int col) {
    if (handle < 0 || handle >= NV_SQLITE_MAX_DB) return "";
    NvSqliteSlot* slot = &g_slots[handle];
    if (!slot->col_names || col < 0 || col >= slot->cols) return "";
    return slot->col_names[col] ? slot->col_names[col] : "";
}

// A cell of the last stored result ("" when out of range).
const char* nv_sqlite_cell(int handle, int row, int col) {
    if (handle < 0 || handle >= NV_SQLITE_MAX_DB) return "";
    NvSqliteSlot* slot = &g_slots[handle];
    if (row < 0 || col < 0 || row >= slot->rows || col >= slot->cols) return "";
    const char* v = slot->cells[slot->rows * slot->cols ? row * slot->cols + col : 0];
    return v ? v : "";
}

// ── Narval-facing wrappers for the materialised result set ────────────────
NvObject* nv_sqlite_query_run_builtin(NvObject* handle, NvObject* sql) {
    return nv_sqlite_box_i(nv_sqlite_query_run(nv_sqlite_arg_int(handle), nv_sqlite_arg_str(sql)));
}

NvObject* nv_sqlite_col_count_builtin(NvObject* handle) {
    return nv_sqlite_box_i(nv_sqlite_col_count(nv_sqlite_arg_int(handle)));
}

NvObject* nv_sqlite_cell_builtin(NvObject* handle, NvObject* row, NvObject* col) {
    return nv_sqlite_box_s(nv_sqlite_cell(nv_sqlite_arg_int(handle), nv_sqlite_arg_int(row),
                                          nv_sqlite_arg_int(col)));
}

NvObject* nv_sqlite_col_name_builtin(NvObject* handle, NvObject* col) {
    return nv_sqlite_box_s(nv_sqlite_col_name(nv_sqlite_arg_int(handle),
                                              nv_sqlite_arg_int(col)));
}
