#!/usr/bin/env bash
# Narval integration tests — one case per file under tests/cases/.
#
#   <name>.nv        the program
#   <name>.out       expected stdout, byte for byte (absent: expect no output)
#   <name>.rc        expected exit code (default 0)
#   <name>.err       substring the diagnostics must mention; the program must then be
#                    REJECTED (the exit code is not compared)
#   <name>.in        stdin for the program (default: empty)
#   <name>.files/    extra files copied next to the .nv: headers for
#                    `comptime import_c`, modules the case imports (symlinks to the
#                    real stdlib, so the test runs against the real file)
#
# A case whose source contains `@[no_std]` is built as a freestanding binary and run;
# everything else is executed through the JIT, like a user would.
#
# Usage: run.sh [path-to-narval-binary]      (default: <repo>/build/narval)

set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cases="$here/cases"
repo="$(cd "$here/.." && pwd)"
narval="${1:-$repo/build/narval}"

if [ ! -x "$narval" ]; then
    echo "run.sh: no executable narval binary at '$narval'" >&2
    echo "usage: run.sh [path-to-narval-binary]" >&2
    exit 2
fi
narval="$(cd "$(dirname "$narval")" && pwd)/$(basename "$narval")"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

pass=0
fail=0
failed_names=""

note() { printf '    %s\n' "$1"; }
show() { head -12 < "$1" | sed 's/^/    /'; }

for nv in "$cases"/*.nv; do
    name="$(basename "$nv" .nv)"
    dir="$tmp/$name"
    mkdir -p "$dir"
    cp "$nv" "$dir/$name.nv"
    if [ -d "$cases/$name.files" ]; then
        cp -R -L "$cases/$name.files/." "$dir/"   # -L: a module symlink is copied as the real file
    fi

    out="$dir/stdout"
    errf="$dir/stderr"
    stdin_file="/dev/null"
    [ -f "$cases/$name.in" ] && stdin_file="$cases/$name.in"

    rc=0
    if grep -q '@\[no_std\]' "$nv"; then
        # Freestanding programs are not JIT-executed: build and run the binary.
        if ! (cd "$dir" && "$narval" -b "$name.nv" >"$dir/build.log" 2>&1); then
            printf '[FAIL] %s: no_std build failed\n' "$name"
            show "$dir/build.log"
            fail=$((fail + 1)); failed_names="$failed_names $name"; continue
        fi
        (cd "$dir" && "./$name" <"$stdin_file" >"$out" 2>"$errf"); rc=$?
    else
        "$narval" "$dir/$name.nv" <"$stdin_file" >"$out" 2>"$errf"; rc=$?
    fi

    if [ -f "$cases/$name.err" ]; then
        needle="$(cat "$cases/$name.err")"
        if [ "$rc" -eq 0 ]; then
            printf '[FAIL] %s: expected a compile-time error, but it compiled\n' "$name"
            fail=$((fail + 1)); failed_names="$failed_names $name"; continue
        fi
        if ! grep -qF -- "$needle" "$errf" && ! grep -qF -- "$needle" "$out"; then
            printf '[FAIL] %s: diagnostics did not mention %s\n' "$name" "$needle"
            show "$errf"
            fail=$((fail + 1)); failed_names="$failed_names $name"; continue
        fi
        printf '[PASS] %s\n' "$name"
        pass=$((pass + 1))
        continue
    fi

    want_rc=0
    [ -f "$cases/$name.rc" ] && want_rc="$(cat "$cases/$name.rc")"
    if [ "$rc" -ne "$want_rc" ]; then
        printf '[FAIL] %s: exit=%s (want %s)\n' "$name" "$rc" "$want_rc"
        show "$errf"
        fail=$((fail + 1)); failed_names="$failed_names $name"; continue
    fi

    want_out="$cases/$name.out"
    [ -f "$want_out" ] || want_out=/dev/null
    if ! cmp -s "$out" "$want_out"; then
        printf '[FAIL] %s: stdout mismatch\n' "$name"
        printf '    --- expected ---\n'; sed 's/^/    /' "$want_out" | head -20
        printf '    --- got ---\n';      sed 's/^/    /' "$out" | head -20
        fail=$((fail + 1)); failed_names="$failed_names $name"; continue
    fi

    printf '[PASS] %s\n' "$name"
    pass=$((pass + 1))
done

printf '=== %d/%d integration tests passed ===\n' "$pass" "$((pass + fail))"
if [ "$fail" -ne 0 ]; then
    printf 'failed:%s\n' "$failed_names"
    exit 1
fi
