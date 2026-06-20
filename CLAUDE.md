# Narval — Compiler

Compiled multiparadigm language with inferred typing. Ownership & Borrowing are **implicit and inferred** — no annotations, no visible borrow errors. The compiler assumes responsibility for memory, parallelism and safety.

---

## Architecture

```
Source (.nv)
  → Lexer        (src/frontend/lexer/)
  → Parser       (src/frontend/parser/)
  → Type Checker (src/frontend/checker/)
  → IR Codegen   (src/backend/codegen/)   ← LLVM IR
  → Runtime      (src/backend/runtime/)   ← C, linked as .o
  → Binary
```

- AST nodes: each has `codegen(IRGenerationContext&)` — visitor pattern
- Runtime types: `Value = { NvObject* obj }` pointing to `NvTypeObject*`
- Class instances: map-backed (`NVMap`) — accessed via `nv_object_get_field` / `nv_object_set_field`
- CMakeLists: uses `GLOB_RECURSE` — new `.cpp` files are auto-detected

---

## Build & Test

```bash
# NUNCA use -j no make, pois crasha
cd build && cmake .. && make Narval && ./narval ../test.nv 
```

> Editing `.c` files in `src/backend/runtime/`: delete `build/lib/runtime.o` first.

---

## Features

### Variables & Types
- `x = 42` inferred; `x: int = 42` explicit; `mut x = 42` mutable
- Primitives: `int`, `float`, `str`, `bool`, `char`
- Collections: `{1,2,3}` array (fixed), `[1,2,3]` vector (dynamic), `(1,"a")` tuple, `{"k": v}` map
- Low-level (for `@[abi]` / `asm`): `i8 i16 i32 i64 i128 u8 u16 u32 u64 u128 f32 f64 usize isize ptr`

### Control Flow
`if/elif/else` · `for x in col` · `while` · `forever` · `break` · `continue` · `match` · ternary `v if cond else other` · list comprehension `[x*2 for x in list]` · ranges `0..10` / `0..=10`

### Functions
```narval
def add(a: int, b: int): int {
    return a + b;
}
```
- Explicit return type; closures: `|x: int|: int { x + 1 }`
- Fallible functions: contain `propagate` → auto-wrapped in `Result`

### Classes & Interfaces
- `extends` (single inheritance), `implements` (multiple interfaces), `super`, `self`
- Modifiers: `public`, `private`, `protected`, `abstract`, `override`
- `instanceof` for type check
- Operator overloading via dunders: `__add__` `__sub__` `__mul__` `__div__` `__eq__` `__lt__` `__str__` etc.

### Enums
```narval
enum Status {
    pending,
    running,
    done = 10,
    failed
}
```
Global map object; accessed via member expression.

### Error Handling
- `try / catch E e { } / finally { }` — `setjmp`/`longjmp` based
- `throw ValueError("msg")`
- `defer error { write(err.message); }` — intercepts errors in remaining scope
- `propagate` — re-throws from fallible functions

### Option / Result + `or`
```narval
a = None;  b = Some(42);  c = Ok(100);  d = Err("fail");
x = None or 0;                          # 0  (fallback)
y = Err("x") or { write(err); return -1; };  # block handler
```

### Attributes & Decorators
```narval
@[no_std]           # module attribute — no runtime
@[abi("sysv64")]    # function ABI (also: win64, C)
@decorator          # function decorator
```

### Imports & Extern
```narval
from "module" import Foo, Bar as B, *;
from extern "C:math" import sqrt;
extern "Python" from "script.py" { def f(x: vector): vector }
from extern "Python:numpy" import * as np;
```
C registry built-in: `math`, `stdlib`, `string`, `stdio`, `time`.

### Assembly

#### Inline ASM — inside `@[abi]` functions
```narval
@[abi("sysv64")]
def add_fast(a: i64, b: i64): i64 {
    asm {
        "add {b}, {a}"     # AT&T template; {name} → $N (outputs indexed first)
    } input {
        reg a;
        reg b;
    } output {
        a -> result;       # result bound in scope after the block
    }
    return result;
}
```
Constraint string auto-built: `=r` per output, tied/`r` per input.

#### Naked ASM — bare-metal entry points
```narval
@[no_std]

naked_asm def _start: asm {
    return `
    mov rax, 60
    mov rdi, 0
    syscall
    `;
}
```
- Intel syntax; emitted as module-level assembly — zero LLVM overhead
- `@[no_std]`: removes runtime, entry point goes directly to linker (`-Wl,-e,_start`)
- Missing entry point → compile error

---

## Implementation Patterns

### Adding a new statement
1. Token in `token.hpp` + keyword in `identifier_tokenizer.cpp`
2. `NodeType` in `types.hpp`
3. AST node in `include/frontend/ast/statements/`
4. Parser in `src/frontend/parser/statements/` + case in `parse_stmt.cpp`
5. Checker in `src/frontend/checker/statements/` + case in `checker_meth.cpp`
6. Codegen in `src/backend/codegen/statements/`

**LSP vtable pattern:** define `clone()` out-of-line in checker `.cpp` (makes it the key function → vtable lives in libchecker). Add `__attribute__((weak)) codegen()` stub there. Force linker to include the codegen `.o` via an anchor symbol referenced from `generate_ir.cpp`.

### Object model
- Constructor: `__ctor_ClassName(Value* __this, Value* p0, ...)`
- Method: `Value __method_ClassName_name(Value* __this, Value* p0, ...)`
- Field r/w: `nv_object_get_field` / `nv_object_set_field`

### Value struct in LLVM IR
`nv.rt.Value.v2` is `{ ptr }`. Never `ExtractValue` expecting an integer tag — use runtime functions. Collection indexing: `array_get_index_v` (read) · `nv_set_at_index` (write).

### Stack alignment
`main.start` receives RSP unaligned. Fixed via `and $$-16, %rsp` InlineAsm in `main.cpp`. Required for `printf` with floats (`movaps`/XMM).

### FFI — add C library to registry
In `src/backend/codegen/ffi.cpp`, inside `get_c_registry()`:
```cpp
{"mylib", { {"func", "float", {{"x", "float"}}}, }},
```
Then: `from extern "C:mylib" import func;`

---

## Type System

### Runtime (`src/backend/runtime/`)

| Type | Constant | C Struct |
|------|----------|----------|
| `int` | `NV_INT_BASE=1` | `NVInt { int32_t value; }` |
| `float` | `NV_FLOAT_BASE=2` | `NVFloat { double value; }` |
| `bool` | `NV_BOOL_BASE=3` | `NVBool { int32_t value; }` |
| `str` | `NV_STR_BASE=4` | `NVStr { char* value; size_t len; }` |
| `array` | `NV_ARRAY_BASE=5` | `NVArray { Value* elements; int size; }` |
| `vector` | `NV_VECTOR_BASE=6` | dynamic |
| `map` | `NV_MAP_BASE=7` | `NVMap { char** keys; Value* values; int size; }` |
| `tuple` | `NV_TUPLE_BASE=8` | dynamic |
| `Error` | `NV_ERROR_BASE=12` | `NVError { char* message; char* traceback; }` |
| `None` | `NV_OPTION_NONE_BASE=21` | — |
| `Some(x)` | `NV_OPTION_SOME_BASE=22` | `{ Value inner; }` |
| `Ok(x)` | `NV_RESULT_OK_BASE=23` | `{ Value inner; }` |
| `Err(x)` | `NV_RESULT_ERR_BASE=24` | `{ Value inner; }` |

### Type Checker (`include/frontend/checker/type.hpp`)
Kinds: `INT FLOAT BOOL STRING VOID FUNCTION ARRAY TUPLE VECTOR MAP CLASS INTERFACE ENUM OPTION RESULT TYPE_VAR POLY_TYPE ERROR LOW_LEVEL`

Hindley-Milner inference with unification. `LOW_LEVEL` maps `i8`..`ptr` directly to LLVM types without Value boxing.

---

## Known Limitations
- `return` inside `try` block doesn't pop the handler stack
- Calling `@[abi]` functions from regular Narval code requires manual boxing (not yet automatic)
- `defer error { }` at module scope not yet implemented
