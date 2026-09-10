#pragma once

// SQLite for Narval programs. Declared for import_c by stdlib/sqlite.nv: a handle
// is a small integer (-1 on failure) and text comes back as a pointer to a buffer
// owned by the handle, overwritten by the next call on it.
int nv_sqlite_open(const char *path);
int nv_sqlite_close(int handle);
int nv_sqlite_exec(int handle, const char *sql);
const char *nv_sqlite_query(int handle, const char *sql);
const char *nv_sqlite_error(int handle);
int nv_sqlite_last_id(int handle);
int nv_sqlite_changes(int handle);
