# Narval — Compilador

Compilador da linguagem Narval, escrito em C/C++. Frontend próprio (lexer → parser → type checker) + backend LLVM. Runtime em C com sistema de tipos orientado a objetos (tudo é um objeto, estilo Python, com prototype chain estilo JavaScript).

---

## Filosofia

Linguagem compilada multiparadigma de alto desempenho com tipagem inferida. Usa Ownership & Borrowing **implícito e inferido** — sem anotações explícitas, sem erros de borrow visíveis ao usuário. O compilador assume a responsabilidade por memória, paralelismo e segurança.

Diferente de Rust, Narval não ensina o programador a escrever código correto — ela faz código comum se comportar como código expert.

---

## Arquitetura

```
Source (.nv)
  → Lexer        (src/frontend/lexer/)
  → Parser       (src/frontend/parser/)
  → Type Checker (src/frontend/checker/)
  → IR Codegen   (src/backend/codegen/)   ← LLVM IR
  → Runtime      (src/backend/runtime/)   ← C, linked as .o
  → Binary
```

- **AST nodes**: cada node tem `codegen(IRGenerationContext&)` — padrão visitor
- **Tipos no runtime**: `Value = { NvObject* obj }`. O objeto aponta para `NvTypeObject*` que identifica o tipo
- **Objetos de classe**: map-backed (`NVMap`) — campos e métodos acessados via `nv_object_get_field` / `nv_object_set_field`
- **CMakeLists**: usa `GLOB_RECURSE` — novos `.cpp` são detectados automaticamente

---

## Compilação e teste

```bash
cd build
cmake ..
make -j4
./narval ../test.nv
```

> **Atenção:** ao editar arquivos `.c` em `src/backend/runtime/`, deletar `build/lib/runtime.o` antes de `make`:
> ```bash
> rm build/lib/runtime.o && make -j4
> ```
> O CMake não detecta mudanças em artifacts pré-compilados.

Há binários de teste separados para lexer, parser e checker na pasta `build/`.

---

## Features implementadas

### Tipos primitivos
- `int`, `float`, `str`, `bool` — com métodos via prototype
- Conversão: `str(x)`, `int(x)`, `float(x)`, `bool(x)`
- Aritmética polimórfica: `nv_value_add/sub/mul/div/mod` suporta int+float automaticamente

### Controle de fluxo
- `if / elif / else`
- `for`, `while`, `forever` (loop infinito), `break`, `continue`
- `match` (pattern matching)
- Ternário: `value if condition else other`
- List comprehension: `[x * 2 for x in list]`
- Ranges: `0..10`, `0..=10`

### Funções
```narval
def soma(a: int, b: int) {
    return a + b
}
```
- Tipagem inferida (`automatic`) ou explícita
- Recursão suportada

### Classes (Python-like)
```narval
class Pessoa {
    nome: str;
    idade: int;

    public new(nome: str, idade: int): void {
        self.nome = nome;
        self.idade = idade;
    }

    public saudacao(): void {
        write("Olá, " + self.nome);
    }

    metodoPrivado(): void {
        write("Sou privado");
    }
}

p = new Pessoa("João", 25);
p.saudacao();
```
- Instâncias são `NVMap` com fields e métodos
- Herança: `class Filho extends Pai { ... }`
- `self` para acesso a membros (antigo `this`)
- `super` para chamar método do pai
- `instanceof` para verificação de tipo
- Modificadores: `public`, `private`, `protected`

### Interfaces
```narval
interface Animal {
    speak(): void
    move(direction: str): void
}

interface Pet extends Animal {
    getName(): str
}

class Dog implements Animal {
    speak(): void {
        write("Woof!");
    }
    move(direction: str): void {
        write("Moving: " + direction);
    }
}
```
- Apenas assinaturas de métodos — sem campos, sem implementação
- Uma classe pode implementar múltiplas interfaces: `implements A, B, C`
- Interface pode herdar de outras interfaces: `extends A, B`
- Métodos que implementam interfaces são automaticamente `public`
- Type checker verifica que todos os métodos da interface estão presentes
- Erro claro se método ausente ou se `implements` aponta para não-interface
- Puramente compile-time — zero overhead em runtime

### Operator Overloading
```narval
class Vector {
    x: int;

    public new(x: int): void {
        self.x = x;
    }
    
    public __str__(): str {
        return "Vector(" + self.x + ")";
    }

    public __add__(other: Vector): Vector {
        return new Vector(self.x + other.x);
    }
}
```
Dunders suportados: `__add__`, `__sub__`, `__mul__`, `__div__`, `__floordiv__`, `__mod__`, `__pow__`, `__eq__`, `__ne__`, `__lt__`, `__gt__`, `__le__`, `__ge__`

### Enums
```narval
enum Status {
    pending,        # 0
    running,        # 1
    done = 10,      # 10 (explícito)
    failed          # 11 (continua)
}

write(Status.done);   # 10
```
- Implementados como global map object
- Acesso via member expression normal

### Coleções
- **Array**: `{1, 2, 3}` — tamanho fixo
- **Vector**: `[1, 2, 3]` — dinâmico, métodos `push`, `pop`, `get`, `set`
- **Tuple**: `(1, "a", true)`
- **Map**: `{"chave": valor}` — dict-like

### Tratamento de erros

#### try/catch/finally/throw
```narval
try {
    throw ValueError("mensagem")
} catch ValueError e {
    write(e)
} catch Error {
    write("qualquer erro")
} finally {
    write("sempre executa")
}
```
Implementado via `setjmp`/`longjmp` no runtime C.

#### Option / Result + operador `or`
```narval
# Construtores
a = None;            # Option::None  (keyword, sem parênteses)
b = Some(42);        # Option::Some(42)
c = Ok(100);         # Result::Ok(100)
d = Err("falhou");   # Result::Err("falhou")

# or: intercept failure
x = None or 0;             # 0    (fallback literal)
y = Some(7) or 0;          # 7    (unwrap automático)
z = Ok(100) or 0;          # 100

# or com bloco — return obrigatório para retornar valor
result = Err("ops") or {
    write("erro capturado:");
    write(err);       # `err` é keyword reservada: auto-bound ao Error extraído — só válida dentro de `or { }`
    return -1;        # return redireciona o valor da or-expression (não sai da função)
};

# Chaining
val = fetch() or cache() or default_val;

# propagate (re-lança o erro)
data = fetch() or {
    propagate;
};
```

**Semântica do `or`:**
- Se o valor é `None` ou `Err` → executa o handler
- Se é qualquer outro valor → passa direto (sem custo extra)
- No bloco, `err` é **keyword reservada** — auto-bound ao `Error` extraído — só válida dentro de `or { }`
- `return value` dentro do bloco define o valor da expressão `or` (não retorna da função)
- `propagate` re-lança via `nv_throw_exception`
- Sem `return` no bloco → resultado é null (side-effects apenas)

### Classe Error (builtin)
```narval
class MeuErro extends Error {
    code: int;
    message: str;
    
    public new(msg: str, code: int): void {
        self.message = msg;
        self.code = code;
    }
}
```
- `Error` tem campo `message: str`
- Hierarquia builtin: `ValueError`, `TypeError`, `RuntimeError`, `IndexError`, `KeyError`, `AttributeError`, `NameError`, `AssertionError`

### Imports
```narval
from "modulo" import Foo, Bar;
from "modulo" import Baz as B;
from "modulo" import *;
from "modulo" import * as Y;
```

### Funções builtin
`write(x)`, `read()`, `exit(code)`, `str(x)`, `int(x)`, `float(x)`, `bool(x)`, `Some(x)`, `Ok(x)`, `Err(x)`

### Keywords literais
`None` — Option::None (sem parênteses, equivalente a `null` para faltáveis)

### Modos interativos
- **REPL**: `./narval --repl` (ou `-i`, `-r`)
- **Notebook**: `./narval --notebook` (ou `-n`) — Python-like

---

### `defer error { }`

Intercepta qualquer erro lançado no escopo restante após o `defer`. O bloco handler recebe `err` auto-bound ao erro capturado.

```narval
defer error {
    write("erro capturado: " + err.message);
}
# qualquer throw/propagate abaixo vai para o handler acima
risky_call();
```

- Implementado via `setjmp`/`longjmp` no runtime, igual ao `try/catch`
- O parser coleta o `remaining_body` (statements após o `defer`) como corpo implícito do try
- AST node: `DeferErrorStmtNode { handler_body, remaining_body }`

---

### Propagação implícita de `or` (funções falíveis)

Funções que contêm `propagate` são automaticamente marcadas como **falíveis** pelo checker. Elas retornam `Result::Ok(valor)` no caminho normal e `Result::Err(erro)` no `propagate`, sem anotação explícita do usuário.

```narval
def fetch_data(url: str) {
    data = http_get(url) or { propagate; };
    return data;
}

# Chamador usa `or` para tratar falha:
result = fetch_data("https://...") or { write(err.message); return; };
```

- O checker detecta `propagate` recursivamente no corpo da função e seta `is_fallible = true`
- O codegen força `ret_ty = ValueTy` e envolve `return x` em `create_result_ok(&slot, &x_slot)`
- `propagate` dentro de função falível emite `create_result_err` + `ret`; fora emite `nv_throw_exception`
- `check_return_stmt` pula verificação de tipo em funções falíveis

---

### Interoperabilidade com linguagens externas (`extern`)

#### Declaração manual (qualquer linguagem com ABI C)

```narval
extern "C" {
    def sqrt(x: float): float
    def printf(fmt: str): int
}

result = sqrt(2.0);
```

- O codegen gera um wrapper LLVM que extrai os args do `Value`, chama a função C nativa, e box o resultado de volta para `Value`
- Suporta: C, C++, Assembly, Rust, Go — qualquer coisa com ABI C

#### Import de biblioteca C via registry

```narval
from extern "C:math" import sqrt;
from extern "C:math" import sqrt, sin, cos;
from extern "C:math" import *;
```

Bibliotecas com registry embutido: `math`, `stdlib`, `string`, `stdio`, `time`.

```narval
from extern "C:math" import sqrt;
x = sqrt(2.0);
```

#### Interop com Python — arquivo `.py`

```narval
extern "Python" from "meu_script.py" {
    def processar(dados: vector): vector
    def calcular(x: float, y: float): float
}

resultado = processar(minha_lista);
```

- O codegen gera um bridge C que: inicializa `Py_Initialize`, carrega o `.py` via `PyRun_SimpleFile`, e para cada função declarada cria um wrapper que converte `Value` ↔ `PyObject*`
- O bridge é compilado com `gcc` e linkado automaticamente
- Suporte a venv: se `VIRTUAL_ENV` estiver definido, o `site-packages` é adicionado ao `sys.path`

#### Namespace Python (módulo instalado)

```narval
from extern "Python:matplotlib.pyplot" import * as plt;
from extern "Python:numpy" import * as np;

plt.plot([1, 2, 3], [4, 5, 6]);
plt.savefig("grafico.png");
```

- Gera um dispatcher genérico `_nv_py_ns_call_ALIAS(method, args, n)` que chama `PyObject_GetAttrString` + `PyObject_Call`
- Qualquer método do módulo pode ser chamado sem declaração prévia
- Conversão automática `Value` ↔ `PyObject*` para: `int`, `float`, `str`, `bool`, `vector`/`array` (→ list), `map` (→ dict)
- Para display interativo (matplotlib): usar backend `Agg` + `savefig` — modo não-interativo por design

#### Linker extras via `-L` e `NARVAL_LINK_EXTRA`

Bridges gerados em codegen são linkados automaticamente. É possível passar objetos extras:

```bash
./narval programa.nv -L meu_bridge.o -L "-lssl"
# ou via variável de ambiente:
NARVAL_LINK_EXTRA="meu_bridge.o -lssl" ./narval programa.nv
```

#### Arquitetura FFI (`src/backend/codegen/ffi.cpp`)

| Backend | Quando usado | O que gera |
|---------|-------------|------------|
| `CABIBackend` | `extern "C/C++/..."` e `from extern "C:lib"` | wrapper LLVM com extração/boxing de Value |
| `PythonBackend` | `extern "Python" from "file.py"` | bridge .c compilado + declaração LLVM externa |
| `emit_python_namespace` | `from extern "Python:mod" import * as alias` | bridge .c com dispatcher genérico |

Tipos suportados na FFI C (tabela `TypeDesc` em `ffi.hpp`): `str`, `int`, `float`, `bool`, `void`, `vector`, `map`, `tuple`.

---

## Features a implementar

- Ownership & Borrowing implícito e inferido
- `defer error { }` global (no nível de módulo)
- Interop com Rust e Go (requer ABI shim)
- Propagação implícita sem `or { propagate }` explícito

---

## Sistema de tipos interno

### Runtime (`src/backend/runtime/`)

| Tipo | Constante | Struct C |
|------|-----------|----------|
| `int` | `NV_INT_BASE=1` | `NVInt { NvObject_HEAD; int32_t value; }` |
| `float` | `NV_FLOAT_BASE=2` | `NVFloat { NvObject_HEAD; double value; }` |
| `bool` | `NV_BOOL_BASE=3` | `NVBool { NvObject_HEAD; int32_t value; }` |
| `str` | `NV_STR_BASE=4` | `NVStr { NvObject_HEAD; char* value; size_t len; }` |
| `array` | `NV_ARRAY_BASE=5` | `NVArray { NvObject_HEAD; Value* elements; int size; }` |
| `vector` | `NV_VECTOR_BASE=6` | `NVVector { ... }` |
| `map` | `NV_MAP_BASE=7` | `NVMap { NvObject_HEAD; char** keys; Value* values; int size; }` |
| `tuple` | `NV_TUPLE_BASE=8` | `NVTuple { ... }` |
| `Error` | `NV_ERROR_BASE=12` | `NVError { NvObject_HEAD; char* message; char* traceback; }` |
| `Option::None` | `NV_OPTION_NONE_BASE=21` | `NVOptionNone { NvObject_HEAD; }` |
| `Option::Some` | `NV_OPTION_SOME_BASE=22` | `NVOptionSome { NvObject_HEAD; Value inner; }` |
| `Result::Ok` | `NV_RESULT_OK_BASE=23` | `NVResultOk { NvObject_HEAD; Value inner; }` |
| `Result::Err` | `NV_RESULT_ERR_BASE=24` | `NVResultErr { NvObject_HEAD; Value inner; }` |

### Type checker (`include/frontend/checker/type.hpp`)
Kinds: `INT`, `FLOAT`, `BOOL`, `STRING`, `VOID`, `DEF`, `ARRAY`, `TUPLE`, `VECTOR`, `MAP`, `CLASS`, `ENUM`, `OPTION`, `RESULT`, `TYPE_VAR`, `POLY_TYPE`, `ERROR`

Inferência Hindley-Milner com unificação.

---

## Padrões de implementação

### Adicionar novo statement
1. Token em `token.hpp` + keyword em `identifier_tokenizer.cpp`
2. `NodeType` em `types.hpp`
3. AST node em `include/frontend/ast/statements/` (ou `expressions/`)
4. Include em `ast.hpp`
5. Parser em `src/frontend/parser/statements/` + case em `parse_stmt.cpp`
6. Checker em `src/frontend/checker/statements/` + case em `checker_meth.cpp`
7. Codegen em `src/backend/codegen/statements/`

### Object model
- Instâncias = `NVMap` heap-allocated
- Constructor LLVM function: `__ctor_ClassName(Value* __this, Value* p0, ...)`
- Method LLVM function: `Value __method_ClassName_name(Value* __this, Value* p0, ...)`
- Field access: `nv_object_get_field(out, self, "field_name")`
- Field write: `nv_object_set_field(self, "field_name", &val)`

### Stack alignment (importante!)
O entry point `main.start` recebe RSP não alinhado a 16 bytes. Fix aplicado em `main.cpp` via InlineAsm `and $$-16, %rsp`. Necessário para `printf` com floats (usa `movaps`/XMM).

### FFI — adicionar nova biblioteca ao registry C

Em `src/backend/codegen/ffi.cpp`, dentro de `get_c_registry()`:

```cpp
{"minha_lib", {
    {"funcao", "float", {{"x", "float"}}},
}},
```

Depois: `from extern "C:minha_lib" import funcao;`

### FFI — adicionar nova linguagem

1. Criar `class MinhaLinguagemBackend : public Backend` em `ffi.cpp`
2. Implementar `void emit(const ExternStmtNode&, IRGenerationContext&)`
3. Adicionar condição em `backend_for(language, source_file)`

### Value struct no LLVM IR

O struct `nv.rt.Value.v2` é `{ ptr }` — um único campo ponteiro para `NvObject*`. **Nunca** extrair campo com `ExtractValue` esperando um inteiro de tipo; sempre usar funções runtime para inspecionar o tipo (`nv_get_iterable_length`, `nv_set_at_index`, etc.).

Para indexação de coleções:
- Leitura: `array_get_index_v(out, self, i32_idx)`
- Escrita: `nv_set_at_index(self, i32_idx, value)` — despacha internamente por tipo

### Limitação conhecida: try/catch com return
`return` dentro de um bloco `try` não faz pop do handler antes de retornar — o handler stack fica com uma entrada extra. Evitar `return` direto dentro de `try` por enquanto.
