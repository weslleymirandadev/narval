# Narval

A high-performance, multiparadigm compiled programming language with inferred typing that uses implicit and inferred Ownership & Borrowing concepts (YES, without explicit annotations, without borrow errors in the user's face). The compiler assumes responsibility for memory, parallelism, and safety — without imposing a new mental model on the programmer.

Narval transfers the operational responsibility of the code to the compiler, while preserving predictability, performance, and control when needed. If it can be proven safe, Narval does it automatically. Unlike Rust, Narval does not try to teach the programmer how to write correct code; it tries to make ordinary code behave like expert code.

## Install

Binaries are published per platform on the [releases page](https://github.com/weslleymirandadev/narval/releases). Unpack the archive anywhere and put `narval` on your `PATH`.

The archive holds a single executable: the runtime object the compiler links against and the standard library modules travel inside it, so nothing has to be installed next to the binary. The first run writes them to a per-user directory — `~/.cache/narval/<version>` on Linux, `%LOCALAPPDATA%\Narval\<version>` on Windows — and every later run reuses it. Setting `NARVAL_HOME` moves that directory wherever you want it.

Turning a Narval program into an executable ends in a link step that is driven by the platform's C toolchain: `gcc` on Linux, MinGW-w64 `gcc` on Windows. The compiler calls it, so it has to be on `PATH`; the runtime it links in is the embedded one, and no Narval headers or libraries are needed.

LLVM and MLIR are linked into the compiler itself, so the executable depends only on the system libraries a desktop already carries: `libstdc++`, `libm`, `libz` and glibc 2.38 or newer on Linux (the archive is built on Ubuntu 24.04, so an older glibc will not load it). It is about 200 MB on disk and 70 MB in the archive.

## Build from source

Requires CMake 3.20+, a C++17 compiler and LLVM/MLIR 22.1.4.

```sh
cmake -S . -B build -DMLIR_DIR=/usr/local/lib/cmake/mlir -DLLVM_DIR=/usr/local/lib/cmake/llvm
cmake --build build --target Narval -j1
./build/narval --version
```

The build is serial on purpose: the translation units around MLIR/LLVM are large and a parallel build needs more memory than it saves time. `cmake --install` puts `narval` in `bin/` and the runtime object in `lib/narval/`, for the case where the embedded copy is not wanted.

A new source file is picked up by the CMake globs, so re-run `cmake -S . -B build` after adding one. After editing a file under `src/backend/runtime/`, remove `build/lib/runtime.o` before rebuilding: it is a merged relocatable object and the copy embedded in the compiler is regenerated from it.

## The language

```narval
@[derive(eq, debug)]
class User {
    name: str;
    age: int;
    public is_adult(): bool { return self.age >= 18; }
}

def greet(name: str): str {
    return "hello, " + trim(name);
}

u = new User();
u.name = "  bob  ";
u.age = 42;
write(greet(u.name));          # hello, bob
write(u.is_adult());           # true
write(len(split("a b c", " ")));   # 3
```

Classes carry methods and fields, `@[derive(...)]` generates the protocol methods (`eq`, `debug`, `hash`, `ord`, `clone`, `json`) and any name that is not builtin is looked up as a user-written `comptime def derive_<name>`. Ownership is inferred per value — Copy, Move, Borrow or refcount — and never annotated. Compile-time execution is spelled `comptime`: constants, functions evaluated by the compiler, `comptime if` / `comptime for` over known values, reflection (`@typeName`, `@fieldNames`, `@kind`), `@emit` for generating ASTs and `name! { ... }` DSL macros that capture their body as text.

## Usage

```
narval prog.nv                 compile, link and run (the binary is temporary)
narval --build prog.nv         leave ./prog next to the source instead of running it
narval --build=<triple> prog.nv     emit for another target (arm64-v8a, riscv64-elf, ...)
narval --object prog.nv        emit prog.o only, no link
narval --repl                  interactive REPL (JIT)
narval --notebook              notebook mode
narval --enabled-targets       list the targets this build can emit
narval --version, --help
```

Diagnostics: `--emit-nir` prints the module in the Narval dialect as codegen left it, `--dump-passes` prints it after every lowering pass, and `--emit-llvm` prints the LLVM IR after the middle-end. `-L` (or `--link`) adds a library to the link line.

`--explain-ownership` reports what the compiler decided about ownership **in terms of your file**: one line per value, anchored on the source line that produced it, with the value named the way the code names it (`b = a + 1`, not `nv_box_int`):

```
prog.nv — 6 value(s) created in this file
own=fresh value · borrow=temporary read · share=a second reference · move=ownership leaves · mut=place written · drop=released · keep=not reclaimed

module scope (top-level code)  (line 7)
 7 | b = a + 1;
   |   own     "b" — a new value
   |   borrow  "a" — read by an arithmetic operator
   |   drop    "b" — released after its last use

summary: 6 own · 4 drop · 3 borrow · 1 move · 2 share · 1 mut · 1 keep
```

The events are `own` (a fresh value), `borrow` (a temporary read), `share` (a second reference keeps the object alive), `move` (ownership leaves the binding), `mut` (a place is written), `drop` (the owner was released) and `keep` (not reclaimed here, with the reason). The line-number gutter is as wide as the largest number reported, so every `|` lines up, and the report is colored when it goes to a terminal (the source line per token, the event, the names in quotes) — `NO_COLOR=1` turns it off, `NARVAL_COLOR=1` forces it on. `--explain-ownership=all` includes the library functions the program merged (the stdlib prelude) and `--explain-ownership=ir` adds the IR behind each event. Only your file is reported by default: every program carries the stdlib, and reporting it buries your code in someone else's. `NARVAL_DROPS_DEBUG=1` still prints the raw per-IR-value trace, and `NARVAL_DUMP_LOCS=1` makes `--emit-nir`/`--dump-passes` print the source locations (the MLIR printer hides them otherwise).

Environment: `NARVAL_HOME` (where the runtime object lives), `NARVAL_STDLIB` (a standard library directory of your own), `NARVAL_LINK_EXTRA` (extra flags for the link step).

## Standard library

`strings`, `grammar` and `file` are merged into every program, so their functions (`trim`, `split`, `join`, `replace`, `index_of`, `grammar!`, `File`) are usable without an import. `macros` (`sql!`, `regex!`, `route!`) and `sqlite` are libraries you import when you want them:

```narval
from "sqlite" import *;
```

An import is resolved next to the importing file first — that is what makes `from "./lib.nv" import *` work — and then in the bundled standard library, so a program compiles from any directory without a copy of the module beside it. SQLite is loaded at run time through `dlopen`, so a program that uses it needs the system `libsqlite3` installed rather than a link flag.

## Freestanding binaries

`@[no_std]` compiles a program that is static and libc-free, entered through `naked_asm def _start`, `def _start` or `def main`; the runtime there is raw syscalls over a fixed 64 KB arena. It is POSIX-only, and a Windows target is refused with an explicit error instead of producing a binary that cannot run.

## Cross-compilation

`--build=<triple>` emits an object for another LLVM target from the same source. The link step still runs the host `gcc`, so a cross target needs that toolchain reachable as `gcc` — a mingw-w64 `gcc` on `PATH` for a Windows target, for instance. `--object` is the way to get a cross-target object without linking.

## License

MIT. See [LICENSE](LICENSE).
