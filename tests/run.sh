#!/usr/bin/env bash
# Narval integration tests. One case per file: tests/cases/<name>.nv is a program
# that describes itself in comments at the top, so the only thing a case needs is the
# .nv file itself. Nothing is written into the repository: each case runs in a temp
# directory (extra files are materialised there) that is removed at the end.
#
# Directives (comments, so the .nv stays a valid program):
#
#   # Expected:      the stdout the program must print, byte for byte; the lines that
#   # ...            follow are the expected text (one leading "# " is stripped)
#   # Exit code: N   expected exit code (default 0)
#   # Error: <text>  the program must be REJECTED and the diagnostics must mention
#                    <text> (the exit code and stdout are not compared then)
#   # Stdin:         like Expected: the following lines are fed to the program
#   # Module: name = <repo path>   copy a repository file next to the program
#   # File: name     create <name> from the lines that follow
#
# A case whose source contains @[no_std] is built as a freestanding binary and run;
# everything else goes through the JIT, like a user would.
#
# Usage: run.sh [path-to-narval-binary]      (default: <repo>/build/narval)

set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cases="$here/cases"
repo="$(cd "$here/.." && pwd)"

# The compiler never creates these: skipping them keeps a stray file from silently
# turning into a "case".
is_case() { case "$1" in *.nv) return 0;; *) return 1;; esac; }

# True for a line that starts a new directive (ends the block of the current one).
is_directive() {
    case "$1" in
        '# Expected:'|'# Exit code:'*|'# Error:'*|'# Stdin:'|'# Module:'*|'# File:'*) return 0;;
        *) return 1;;
    esac
}

# Reads the directives of one case. Fills: expected, want_rc, want_err, stdin_text and
# materialises "# File:" blocks / copies "# Module:" files into $dir. Parsing stops at
# the first line that is neither a directive nor part of one, so the program's own
# comments are never mistaken for directives.
parse_case() {
    local file="$1" dir="$2"
    expected=""; want_rc=""; want_err=""; stdin_text=""
    local mode="" target="" line spec
    while IFS= read -r line; do
        if is_directive "$line"; then
            mode=""; target=""
            case "$line" in
                '# Expected:')   mode="expected";;
                '# Stdin:')      mode="stdin";;
                '# Exit code:'*) want_rc="${line#\# Exit code: }";;
                '# Error:'*)     want_err="${line#\# Error: }";;
                '# Module:'*)    spec="${line#\# Module: }"
                                 cp -f "$repo/${spec#*= }" "$dir/${spec%% *}" || return 1;;
                '# File:'*)      target="${line#\# File: }"
                                 : > "$dir/$target"; mode="file";;
            esac
            continue
        fi
        # Not a directive: outside a block the header is over and the program starts.
        if [ -z "$mode" ]; then break; fi
        case "$line" in
            '# '*) line="${line#\# }";;     # one separator space is stripped
            '#')   line="";;                # a bare "#" is an empty content line
            *)     break;;                  # blank line or code: the header is over
        esac
        case "$mode" in
            expected) expected="$expected$line
";;
            stdin)    stdin_text="$stdin_text$line
";;
            file)     printf '%s\n' "$line" >> "$dir/$target";;
        esac
    done < "$file"
}

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

for nv in "$cases"/*.nv; do
    is_case "$nv" || continue
    name="$(basename "$nv" .nv)"
    dir="$tmp/$name"
    mkdir -p "$dir"
    cp "$nv" "$dir/$name.nv"

    expected=""; want_rc=""; want_err=""; stdin_text=""
    if ! parse_case "$nv" "$dir"; then
        printf '[FAIL] %s: a "# Module:" file could not be copied\n' "$name"
        fail=$((fail + 1)); failed_names="$failed_names $name"; continue
    fi

    printf '%s' "$stdin_text" > "$dir/stdin"

    out="$dir/stdout"
    errf="$dir/stderr"
    rc=0
    if grep -q '@\[no_std\]' "$nv"; then
        # Freestanding programs are not JIT-executed: build and run the binary.
        if ! (cd "$dir" && "$narval" -b "$name.nv" >"$dir/build.log" 2>&1); then
            printf '[FAIL] %s: no_std build failed\n' "$name"
            head -12 < "$dir/build.log" | sed 's/^/    /'
            fail=$((fail + 1)); failed_names="$failed_names $name"; continue
        fi
        (cd "$dir" && "./$name" <"$dir/stdin" >"$out" 2>"$errf"); rc=$?
    else
        "$narval" "$dir/$name.nv" <"$dir/stdin" >"$out" 2>"$errf"; rc=$?
    fi

    if [ -n "$want_err" ]; then
        if [ "$rc" -eq 0 ]; then
            printf '[FAIL] %s: expected a compile-time error, but it compiled\n' "$name"
            fail=$((fail + 1)); failed_names="$failed_names $name"; continue
        fi
        if ! grep -qF -- "$want_err" "$errf" && ! grep -qF -- "$want_err" "$out"; then
            printf '[FAIL] %s: diagnostics did not mention %s\n' "$name" "$want_err"
            head -12 < "$errf" | sed 's/^/    /'
            fail=$((fail + 1)); failed_names="$failed_names $name"; continue
        fi
        printf '[PASS] %s\n' "$name"; pass=$((pass + 1)); continue
    fi

    [ -n "$want_rc" ] || want_rc=0
    if [ "$rc" -ne "$want_rc" ]; then
        printf '[FAIL] %s: exit=%s (want %s)\n' "$name" "$rc" "$want_rc"
        head -12 < "$errf" | sed 's/^/    /'
        fail=$((fail + 1)); failed_names="$failed_names $name"; continue
    fi

    printf '%s' "$expected" > "$dir/expected"
    if ! cmp -s "$out" "$dir/expected"; then
        printf '[FAIL] %s: stdout mismatch\n' "$name"
        printf '    --- expected ---\n'; sed 's/^/    /' "$dir/expected" | head -20
        printf '    --- got ---\n';      sed 's/^/    /' "$out" | head -20
        fail=$((fail + 1)); failed_names="$failed_names $name"; continue
    fi

    printf '[PASS] %s\n' "$name"; pass=$((pass + 1))
done

printf '=== %d/%d integration tests passed ===\n' "$pass" "$((pass + fail))"
if [ "$fail" -ne 0 ]; then
    printf 'failed:%s\n' "$failed_names"
    exit 1
fi
