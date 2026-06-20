# Compile-Time Execution (CTE) — Narval Specification

## 1. Visão Geral

Execução em tempo de compilação (`comptime`) permite ao compilador Narval avaliar expressões,
funções e blocos **durante a fase de type-checking**, antes de qualquer geração de IR. O
resultado é embutido no AST como literais/constantes que chegam ao codegen já resolvidos.

Isso move trabalho do runtime para compile-time sem custo de abstração, habilitando:
- geração automática de código (serialização, bindings, ORM)
- reflexão de tipos sem overhead
- especialização e unrolling de loops
- tabelas pré-computadas embutidas no binário
- DSLs validadas sem runtime parser
- SIMD e GPU kernel gerados automaticamente

---

## 2. Pipeline de Integração

```
Source (.nv)
  → Lexer        ← novo token COMPTIME
  → Parser       ← novos nós: ComptimeExpr, ComptimeDecl, ComptimeFor, ComptimeIf
  → Checker      ← ComptimeEvaluator roda aqui; substitui nós por literais
  → IR Codegen   ← recebe AST com nós comptime já resolvidos (sem nós comptime restantes)
  → Runtime
  → Binary
```

O checker é onde a magia acontece. Ao visitar um nó `comptime`, o `ComptimeEvaluator` o
interpreta imediatamente e retorna um `ComptimeValue`. Esse valor é convertido num nó literal
e **substitui** o nó comptime no AST. O codegen nunca vê um nó `comptime`.

---

## 3. Sintaxe

### 3.1 Variável comptime

```narval
comptime N = 1024
comptime PI = 3.14159265358979
comptime NAME = "Narval"
```

- Tipo inferido normalmente pelo checker, mas avaliado imediatamente
- Imutável por definição (sem `mut comptime`)
- Visível no escopo como constante; usável em tipos, ranges, arrays

### 3.2 Função comptime

```narval
comptime def fib(n: int): int {
    if n <= 1 { return n; }
    return fib(n - 1) + fib(n - 2);
}

comptime X = fib(20)   # avaliado em compile-time; X = 6765
```

- Pode chamar outras funções `comptime`
- Não pode chamar funções de runtime (I/O, alocação dinâmica, FFI não-comptime)
- Recursão permitida (profundidade máxima configurável, default 1024)

### 3.3 Bloco comptime

```narval
comptime {
    # Tudo aqui roda durante compilação
    val = heavy_calculation(100)
}
```

### 3.4 if comptime

```narval
comptime if PLATFORM == "linux" {
    # Incluído apenas em builds Linux
} else {
    # Caso contrário
}
```

Ramos não tomados são **removidos do AST** — não geram IR.

### 3.5 for comptime (loop unrolling)

```narval
comptime for i in 0..4 {
    sum += arr[i]
}
```

Expande para:

```narval
sum += arr[0]
sum += arr[1]
sum += arr[2]
sum += arr[3]
```

O compilador emite as 4 instruções sequencialmente, sem branch nem comparação de loop.

### 3.6 Reflexão de tipo

```narval
comptime for field in type.fields(User) {
    write(field.name)      # "name", "age", ...
    write(field.type_name) # "str", "int", ...
}
```

Funções de reflexão disponíveis em contexto `comptime`:

| Função | Retorno |
|---|---|
| `type.fields(T)` | array de `{name: str, type_name: str, index: int}` |
| `type.methods(T)` | array de `{name: str, return_type: str, params: [...]}` |
| `type.name(T)` | `str` |
| `type.kind(T)` | `"class"`, `"struct"`, `"enum"`, `"interface"` |
| `type.has_field(T, "name")` | `bool` |
| `type.param_count(T)` | `int` (para funções) |

### 3.7 Importação C em compile-time

```narval
comptime import_c("stdio.h")
comptime import_c("mylib.h", link: "mylib")
```

O compilador parseia o header e registra funções/structs no namespace como se fossem
`from extern "C:..."` — mas sem precisar de entrada manual no registry `ffi.cpp`.

### 3.8 DSL blocks (quote/splice)

```narval
sql! {
    SELECT * FROM users WHERE age > 18
}
```

`sql!` é uma `comptime def` que recebe o bloco como `str` cru e retorna um objeto
`PreparedQuery`. O processamento (validação, parsing, geração do objeto) ocorre em
compile-time. Nenhum parser SQL existe em runtime.

Sintaxe de definição de macro DSL:

```narval
comptime def sql!(src: str): PreparedQuery {
    # parseia src em compile-time
    return build_query(src)
}
```

### 3.9 Derive automático

```narval
@derive(json, hash, eq, debug)
class User {
    name: str
    age: int
}
```

`@derive` é um decorator especial processado pelo checker via `ComptimeEvaluator`. Ele
injeta métodos gerados no `ClassStmtNode` antes do codegen.

### 3.10 Especialização de função

```narval
comptime def matmul<T, M: int, N: int, K: int>(
    a: Tensor<T, [M, K]>,
    b: Tensor<T, [K, N]>
): Tensor<T, [M, N]> { ... }
```

Cada combinação `(T, M, N, K)` gera uma versão especializada e otimizada. Parâmetros
constantes inteiros como `M`, `N`, `K` permitem unrolling, tiling e emissão SIMD específica.

---

## 4. Implementação — Fase a Fase

### 4.1 Lexer

**Arquivo:** `include/frontend/lexer/token.hpp`

Adicionar ao enum `TokenType`:

```cpp
COMPTIME,      // comptime
HASH_BANG,     // !  (para macros: sql!, regex!, ...)
BACKTICK,      // `  (para strings raw em DSL)
```

**Arquivo:** `src/frontend/lexer/identifier_tokenizer.cpp`

Adicionar ao map de keywords:

```cpp
{"comptime", TokenType::COMPTIME},
```

### 4.2 Parser — Novos Nós AST

#### `NodeType` (`include/frontend/ast/types.hpp`)

```cpp
ComptimeDecl,        // comptime x = expr
ComptimeFuncDef,     // comptime def name(...) { }
ComptimeBlock,       // comptime { ... }
ComptimeFor,         // comptime for i in range { }
ComptimeIf,          // comptime if cond { } else { }
ComptimeExpr,        // comptime <expr>  (inline)
TypeReflectExpr,     // type.fields(T), type.name(T), ...
DeriveAttr,          // @derive(...)
MacroCall,           // sql! { ... }
TensorType,          // Tensor<T, [M, N]>
```

#### `include/frontend/ast/statements/comptime_decl_node.hpp`

```cpp
class ComptimeDeclNode : public Stmt {
public:
    std::string name;
    std::unique_ptr<Expr> value;

    ComptimeDeclNode(std::string name, std::unique_ptr<Expr> value)
        : Stmt(NodeType::ComptimeDecl),
          name(std::move(name)), value(std::move(value)) {}

    Node* clone() const override { ... }
    void codegen(nv::IRGenerationContext& ctx) override;
};
```

#### `include/frontend/ast/statements/comptime_func_node.hpp`

```cpp
class ComptimeFuncNode : public Stmt {
public:
    std::string name;
    std::vector<ParamNode> parameters;
    std::string return_type;
    CodeBlock body;

    ComptimeFuncNode(...) : Stmt(NodeType::ComptimeFuncDef) {}
    Node* clone() const override { ... }
    void codegen(nv::IRGenerationContext& ctx) override; // no-op; avaliado no checker
};
```

#### `include/frontend/ast/statements/comptime_for_node.hpp`

```cpp
class ComptimeForNode : public Stmt {
public:
    std::string var;
    std::unique_ptr<Expr> range;    // RangeExprNode ou outro iterável
    CodeBlock body;

    ComptimeForNode(std::string var, std::unique_ptr<Expr> range, CodeBlock body)
        : Stmt(NodeType::ComptimeFor),
          var(std::move(var)), range(std::move(range)), body(std::move(body)) {}

    Node* clone() const override { ... }
    void codegen(nv::IRGenerationContext& ctx) override; // substituído pelo checker
};
```

#### `include/frontend/ast/statements/comptime_if_node.hpp`

```cpp
class ComptimeIfNode : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    CodeBlock then_body;
    CodeBlock else_body;   // pode ser vazio

    Node* clone() const override { ... }
    void codegen(nv::IRGenerationContext& ctx) override; // substituído pelo checker
};
```

#### `include/frontend/ast/expressions/type_reflect_expr_node.hpp`

```cpp
class TypeReflectExprNode : public Expr {
public:
    std::string fn;          // "fields", "methods", "name", "kind", "has_field"
    std::string target_type; // nome do tipo inspecionado
    std::string extra_arg;   // para has_field: nome do campo

    Node* clone() const override { ... }
    llvm::Value* codegen(nv::IRGenerationContext& ctx) override; // não deve chegar aqui
};
```

### 4.3 Parser — Regras de Produção

**Arquivo:** `src/frontend/parser/statements/parse_stmt.cpp`

```cpp
case TokenType::COMPTIME: {
    advance(); // consome 'comptime'
    if (current().type == TokenType::DEF)       return parse_comptime_func();
    if (current().type == TokenType::FOR)       return parse_comptime_for();
    if (current().type == TokenType::IF)        return parse_comptime_if();
    if (current().type == TokenType::OBRACE)    return parse_comptime_block();
    if (current().type == TokenType::IDENTIFIER &&
        peek().type == TokenType::ASSIGNMENT)   return parse_comptime_decl();
    // comptime <expr> inline
    return parse_comptime_expr();
}
```

**`parse_comptime_func()`** — igual a `parse_function_stmt()` mas cria `ComptimeFuncNode`.

**`parse_comptime_for()`:**

```
'for' IDENTIFIER 'in' expr '{' stmts '}'
→ ComptimeForNode { var, range, body }
```

**`parse_comptime_if()`:**

```
'if' expr '{' stmts '}' ('else' '{' stmts '}')?
→ ComptimeIfNode { condition, then_body, else_body }
```

**Arquivo:** `src/frontend/parser/expressions/parse_type.cpp`

Adicionar parsing de `Tensor<T, [M, N]>`:

```
'Tensor' '<' type ',' '[' int (',' int)* ']' '>'
→ TensorTypeNode { element_type, dims: [M, N, ...] }
```

**Macros DSL** — `src/frontend/parser/expressions/parse_expr.cpp`:

```
IDENTIFIER '!' '{' raw_content '}'
→ MacroCallNode { name, raw_src }
```

O conteúdo bruto entre `{ }` é capturado como string sem parsing.

### 4.4 Checker — ComptimeEvaluator

Este é o componente central. Arquivo novo:

**`include/frontend/checker/comptime_evaluator.hpp`**
**`src/frontend/checker/comptime_evaluator.cpp`**

#### 4.4.1 ComptimeValue

```cpp
namespace nv {

struct ComptimeValue {
    enum class Tag {
        Int, Float, Bool, Str,
        Array,   // vector<ComptimeValue>
        Struct,  // map<string, ComptimeValue>
        Type,    // reflexão: representa um tipo
        Field,   // {name, type_name, index}
        Method,  // {name, return_type, params}
        Void,
        None_,
    };

    Tag tag;
    int64_t  i_val;
    double   f_val;
    bool     b_val;
    std::string s_val;
    std::vector<ComptimeValue> arr_val;
    std::unordered_map<std::string, ComptimeValue> struct_val;
    std::shared_ptr<Type> type_val;  // para Tag::Type / Tag::Field

    static ComptimeValue from_int(int64_t v)       { ComptimeValue c; c.tag = Tag::Int;   c.i_val = v; return c; }
    static ComptimeValue from_float(double v)      { ComptimeValue c; c.tag = Tag::Float; c.f_val = v; return c; }
    static ComptimeValue from_bool(bool v)         { ComptimeValue c; c.tag = Tag::Bool;  c.b_val = v; return c; }
    static ComptimeValue from_str(std::string v)   { ComptimeValue c; c.tag = Tag::Str;   c.s_val = std::move(v); return c; }
    static ComptimeValue from_array(std::vector<ComptimeValue> v) {
        ComptimeValue c; c.tag = Tag::Array; c.arr_val = std::move(v); return c;
    }
    bool is_truthy() const;
    std::string to_string() const;
};

} // namespace nv
```

#### 4.4.2 Classe ComptimeEvaluator

```cpp
namespace nv {

class ComptimeEvaluator {
public:
    explicit ComptimeEvaluator(Checker& checker);

    // Avalia uma expressão em contexto comptime
    ComptimeValue eval(Expr* expr);

    // Avalia um bloco de statements; retorna o valor do último return
    ComptimeValue eval_block(const CodeBlock& body);

    // Registra uma função comptime no escopo
    void register_func(ComptimeFuncNode* node);

    // Resolve type reflection: type.fields(T), type.name(T), etc.
    ComptimeValue eval_type_reflect(TypeReflectExprNode* node);

    // Converte ComptimeValue de volta num Expr literal para substituir no AST
    std::unique_ptr<Expr> to_literal(const ComptimeValue& val, PositionData* pos);

    // Expande comptime for → sequência de Stmts clonados com var substituída
    std::vector<std::unique_ptr<Stmt>> expand_for(ComptimeForNode* node);

    // Avalia comptime if → retorna o ramo correto (ou vazio)
    std::vector<std::unique_ptr<Stmt>> expand_if(ComptimeIfNode* node);

private:
    Checker& checker_;
    int call_depth_ = 0;
    static constexpr int MAX_DEPTH = 1024;

    // Escopo próprio para variáveis comptime
    std::vector<std::unordered_map<std::string, ComptimeValue>> scope_stack_;

    // Funções comptime registradas
    std::unordered_map<std::string, ComptimeFuncNode*> comptime_funcs_;

    ComptimeValue eval_binary(BinaryExprNode* node);
    ComptimeValue eval_call(CallExprNode* node);
    ComptimeValue eval_identifier(IdentifierNode* node);
    ComptimeValue eval_for_range(int64_t start, int64_t end, bool inclusive,
                                  const std::string& var, const CodeBlock& body);
    void push_scope();
    void pop_scope();
    void set_var(const std::string& name, ComptimeValue val);
    ComptimeValue* lookup_var(const std::string& name);
};

} // namespace nv
```

#### 4.4.3 Integração no Checker

**`src/frontend/checker/checker_meth.cpp`** — adicionar cases:

```cpp
case NodeType::ComptimeDecl: {
    auto* node = static_cast<ComptimeDeclNode*>(stmt);
    ComptimeValue val = comptime_eval_.eval(node->value.get());
    comptime_eval_.set_var(node->name, val);
    // Registra no checker como constante (tipo inferido do ComptimeValue)
    auto type = comptime_value_to_type(val);
    checker_.define(node->name, type, /*is_comptime=*/true);
    // Substitui o nó por uma DeclStmtNode com literal
    replace_stmt_with_literal(stmt_idx, val, node->name, node->position.get());
    break;
}

case NodeType::ComptimeFuncDef: {
    auto* node = static_cast<ComptimeFuncNode*>(stmt);
    comptime_eval_.register_func(node);
    // Não gera IR — remove o nó do bloco atual
    remove_stmt(stmt_idx);
    break;
}

case NodeType::ComptimeFor: {
    auto* node = static_cast<ComptimeForNode*>(stmt);
    auto expanded = comptime_eval_.expand_for(node);
    // Substitui o ComptimeForNode pelos stmts expandidos
    replace_stmt_with_block(stmt_idx, std::move(expanded));
    check_stmts_in_place(stmt_idx, expanded_count); // type-check dos stmts injetados
    break;
}

case NodeType::ComptimeIf: {
    auto* node = static_cast<ComptimeIfNode*>(stmt);
    auto chosen = comptime_eval_.expand_if(node);
    replace_stmt_with_block(stmt_idx, std::move(chosen));
    break;
}
```

**`src/frontend/checker/expressions/check_primary_expr.cpp`** — para `comptime <expr>` inline:

```cpp
case NodeType::ComptimeExpr: {
    auto* node = static_cast<ComptimeExprNode*>(expr);
    ComptimeValue val = comptime_eval_.eval(node->inner.get());
    // Substituir o nó pelo literal correspondente
    *expr_ptr = comptime_eval_.to_literal(val, node->position.get());
    return comptime_value_to_type(val);
}
```

**Type reflection** — `check_call_expr.cpp` ou `check_member_expr.cpp`:

Detectar padrão `type.fields(T)`, `type.name(T)` etc. em contexto comptime e delegar a
`comptime_eval_.eval_type_reflect(...)`.

### 4.5 Codegen

O codegen **não precisa de mudanças** para a maioria dos casos — o checker já substituiu
todos os nós comptime por literais antes do codegen rodar.

Exceções onde o codegen precisa agir:

1. **`ComptimeDeclNode` residual** — nunca deve chegar; se chegar, emitir `unreachable`.
2. **`TensorTypeNode`** — tipo novo que codegen precisa representar (ver Seção 8).
3. **`@derive` injeta métodos** no `ClassStmtNode` antes do codegen — esses métodos são
   AST normais que o codegen processa normalmente.

---

## 5. Especificações de Funcionalidade

### 5.1 Geração Automática de Código — `@derive`

**Derive disponíveis:**

| Derive | Métodos gerados |
|---|---|
| `json` | `to_json(): str`, `from_json(s: str): Self` (static) |
| `hash` | `__hash__(): int` |
| `eq` | `__eq__(other: Self): bool` |
| `ord` | `__lt__(other: Self): bool`, `__gt__` |
| `debug` | `__str__(): str` (formato `User { name: "x", age: 42 }`) |
| `clone` | `clone(): Self` |
| `sql` | `to_row(): map`, `from_row(r: map): Self` (static) |
| `openapi` | `schema(): map` — schema OpenAPI 3.0 do tipo |

**Implementação:**

`@derive` é processado em `src/frontend/checker/statements/check_class_stmt.cpp`.

O checker, ao encontrar `@[derive(...)]` na lista de atributos de uma classe:

1. Usa `ComptimeEvaluator` para inspecionar os campos via `type.fields(ClassName)`
2. Gera `FunctionStmtNode`s sinteticamente (em C++ no checker)
3. Injeta esses nós em `ClassStmtNode::methods` antes do checagem dos métodos
4. Os métodos injetados são type-checked e codegenned normalmente

**Exemplo de geração de `to_json` para `User { name: str, age: int }`:**

O checker gera sinteticamente o equivalente a:

```narval
def to_json(): str {
    return "{\"name\": " + self.name + ", \"age\": " + str(self.age) + "}"
}
```

mas via AST construído em C++, sem parsing de string.

### 5.2 Reflexão de Tipos

Implementada no `ComptimeEvaluator::eval_type_reflect()`.

O checker já tem o namespace/prototype de cada tipo em `Checker::env_`. A reflexão lê
essas informações e as expõe como `ComptimeValue::Array` de structs.

**`type.fields(T)`** retorna:

```cpp
// Para cada campo em ClassStmtNode::fields:
ComptimeValue field;
field.tag = ComptimeValue::Tag::Struct;
field.struct_val["name"]      = ComptimeValue::from_str(f->name);
field.struct_val["type_name"] = ComptimeValue::from_str(f->type);
field.struct_val["index"]     = ComptimeValue::from_int(idx);
```

**Uso para serialização automática:**

```narval
comptime def make_json_body(T): str {
    parts = []
    comptime for field in type.fields(T) {
        parts.push("\"" + field.name + "\": " + field.name)
    }
    return "{" + join(parts, ", ") + "}"
}
```

### 5.3 Loop Unrolling

`ComptimeEvaluator::expand_for()`:

1. Avalia o range (`RangeExprNode`) → `start`, `end`, `inclusive`
2. Para cada `i` no range:
   a. Clona o `body` do loop
   b. Substitui todas as ocorrências do identificador `var` por `NumericLiteralNode(i)`
   c. Adiciona ao vetor de stmts expandidos
3. Retorna o vetor — o checker o injeta no lugar do `ComptimeForNode`

**Constraint:** range deve ser resolvível em compile-time (ambos os lados constantes ou
variáveis comptime). Se não for, erro de compilação:
```
error: comptime for requires a compile-time range; 'n' is not comptime
```

### 5.4 Tensor Shape Inference

**Tipo novo:** `TensorType` (Kind: `TENSOR`)

```cpp
struct TensorType : public Type {
    std::shared_ptr<Type> element;   // float32, int, ...
    std::vector<int64_t>  dims;      // [-1 = dinâmico, N = estático]

    TensorType(std::shared_ptr<Type> elem, std::vector<int64_t> dims)
        : Type(Kind::TENSOR), element(std::move(elem)), dims(std::move(dims)) {}

    std::string toString() override { ... }
    bool equals(const Type& other) const override { ... }
};
```

**Inferência de multiplicação matricial:**

Em `check_binary_expr.cpp`, ao detectar `*` entre dois `TensorType`:

```cpp
if (lhs_type->kind == Kind::TENSOR && rhs_type->kind == Kind::TENSOR) {
    auto* lt = static_cast<TensorType*>(lhs_type.get());
    auto* rt = static_cast<TensorType*>(rhs_type.get());

    // [M, K] × [K, N] → [M, N]
    if (lt->dims.size() == 2 && rt->dims.size() == 2) {
        if (lt->dims[1] != rt->dims[0] && lt->dims[1] != -1 && rt->dims[0] != -1) {
            error("shape mismatch: [{}, {}] × [{}, {}]",
                  lt->dims[0], lt->dims[1], rt->dims[0], rt->dims[1]);
        }
        auto result = make_shared<TensorType>(lt->element,
                                              vector<int64_t>{lt->dims[0], rt->dims[1]});
        return result;
    }
}
```

Erros de shape são emitidos em **compile-time**, antes de qualquer execução.

**Broadcasting** — regra NumPy em compile-time:

```
[1, 128] + [32, 128] → [32, 128]   ✓
[32, 64] + [32, 128] → erro        ✗
```

**Codegen de Tensor:**

`TensorType` compila para `nv.rt.Value` apontando para um `NVArray` de `NVArray`s (para
tensores 2D+), ou diretamente para buffer SIMD quando specializado (ver Seção 5.9).

### 5.5 Import C em Compile-Time

```narval
comptime import_c("stdio.h")
comptime import_c("raylib.h", link: "raylib")
```

**Implementação** em `src/frontend/checker/comptime_evaluator.cpp`:

`eval_import_c(path, link_name)`:
1. Executa `clang -cc1 -ast-dump=json <path>` via `popen()` durante o checker
2. Parseia o JSON dump e extrai `FunctionDecl`, `TypedefDecl`, `RecordDecl`
3. Registra funções como `ExternFuncEntry` no registry de FFI (igual ao `ffi.cpp`)
4. Registra structs como tipos `LOW_LEVEL` no namespace do checker
5. Se `link` fornecido, adiciona `-l<link>` à lista de linker flags do `IRGenerationContext`

Alternativa sem Clang: parser de C mínimo (só declarações, sem corpo) embutido no
compilador para headers simples.

### 5.6 DSL Blocks — Macros Procedurais

**Definição de macro:**

```narval
comptime def sql!(src: str): PreparedQuery {
    tokens = tokenize_sql(src)
    ast    = parse_sql(tokens)
    validate_sql(ast)
    return PreparedQuery { query: src, params: extract_params(ast) }
}
```

`tokenize_sql`, `parse_sql`, `validate_sql` são também `comptime def`, formando um
mini-compilador SQL que roda durante o build.

**Parsing de macro no parser:**

```
IDENTIFIER '!' '{' <tudo até o '}' balanceado> '}'
```

O conteúdo bruto é passado como `str` literal para a função comptime.

**Checagem:** O checker vê `MacroCallNode`, verifica que existe uma `comptime def`
com nome `<name>!`, avalia a chamada via `ComptimeEvaluator`, e substitui o nó pelo
`ComptimeValue` resultante convertido em literal.

**DSLs úteis para Narval:**

| Macro | Uso |
|---|---|
| `sql! { ... }` | queries SQL com validação de schema |
| `regex! { ... }` | regex compilado para DFA em compile-time |
| `json! { ... }` | JSON literal com type-check |
| `route! { ... }` | rotas HTTP com validação de placeholders |
| `glsl! { ... }` | shaders validados em compile-time |
| `proto! { ... }` | Protobuf schema → tipos Narval |

### 5.7 Geração de Tabelas em Compile-Time

```narval
comptime sin_table = generate_sin_table(1024)
comptime crc32_table = generate_crc32_table()
comptime fft_twiddles = generate_twiddle_factors(512)
```

O array resultante é embutido no binário como dados constantes (seção `.rodata`).

**Codegen de array comptime:**

Em `src/backend/codegen/`, ao encontrar uma `DeclStmtNode` marcada como `is_comptime`
com valor `ComptimeValue::Array`:

```cpp
// Gera global constant no IR:
// @sin_table = internal constant [1024 x %nv.rt.Value.v2] [...]
auto* arr_type = llvm::ArrayType::get(value_type, val.arr_val.size());
auto* init = llvm::ConstantArray::get(arr_type, constants);
auto* gv = new llvm::GlobalVariable(module, arr_type, /*isConst=*/true,
                                     llvm::GlobalValue::InternalLinkage, init, name);
```

Acesso em runtime: indexação normal via `array_get_index_v`, mas o compilador pode
promover para `getelementptr` direto em `LOW_LEVEL` se o índice for constante.

### 5.8 Especialização e SIMD Automático

```narval
@vectorize
for i in 0..N {
    c[i] = a[i] + b[i]
}
```

O atributo `@vectorize` instrui o checker/codegen a:

1. **Verificar** que o corpo do loop é vetorizável (sem dependências de loop)
2. **Gerar** a versão escalar como fallback
3. **Gerar** versões AVX2/AVX-512/NEON via LLVM auto-vectorization hints

**Implementação via LLVM:**

```cpp
// Em generate_for_stmt.cpp, ao detectar @vectorize:
loop_body_bb->setName("vectorized.body");
// Adicionar metadata de vectorization width:
llvm::MDNode* vec_md = llvm::MDNode::get(ctx.context, {
    llvm::MDString::get(ctx.context, "llvm.loop.vectorize.enable"),
    llvm::ConstantInt::get(llvm::Type::getInt1Ty(ctx.context), 1)
});
llvm::MDNode* width_md = llvm::MDNode::get(ctx.context, {
    llvm::MDString::get(ctx.context, "llvm.loop.vectorize.width"),
    llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx.context), 8) // AVX2: 8×float
});
branch_inst->setMetadata("llvm.loop", llvm::MDNode::get(ctx.context, {vec_md, width_md}));
```

**Especialização por arquitetura:**

```narval
comptime ARCH = target.arch()   # "x86_64", "aarch64", ...
comptime SIMD = target.simd()   # "avx2", "avx512", "neon", "scalar"

comptime if SIMD == "avx2" {
    # versão AVX2
} elif SIMD == "neon" {
    # versão NEON
} else {
    # fallback escalar
}
```

`target.arch()` e `target.simd()` são built-ins comptime que consultam o triple LLVM.

### 5.9 Auto-Diferenciação

Transforma funções matemáticas em suas derivadas automaticamente em compile-time:

```narval
comptime def autodiff(f: comptime_fn, var: str): comptime_fn {
    # analisa o AST de f e retorna o AST da derivada em relação a var
}

def loss(x: float): float { return x*x + sin(x) }

comptime dloss_dx = autodiff(loss, "x")
# equivalente a: def dloss_dx(x: float): float { return 2*x + cos(x) }
```

**Implementação:**

`autodiff` opera sobre o AST da função (via `type.ast(f)`, uma extensão de reflexão).
Aplica regras de diferenciação simbólica:

| Expressão | Derivada |
|---|---|
| `c` (constante) | `0` |
| `x` (variável alvo) | `1` |
| `a + b` | `da + db` |
| `a * b` | `da*b + a*db` |
| `sin(x)` | `cos(x) * dx` |
| `cos(x)` | `-sin(x) * dx` |
| `exp(x)` | `exp(x) * dx` |
| `log(x)` | `(1/x) * dx` |

O resultado é um novo `FunctionStmtNode` injetado no AST — compilado normalmente.

**`type.ast(f)`** — nova função de reflexão que retorna o AST de uma função como
`ComptimeValue::Struct` navegável. Implementada no `ComptimeEvaluator` lendo o
`ComptimeFuncNode` (ou `FunctionStmtNode` marcado com `@comptime_visible`) registrado.

### 5.10 ECS (Entity Component System) Especializado

```narval
@archetype
class Movement {
    position: Position
    velocity: Velocity
}

# query especializada — sem reflection runtime
for entity in query<Position, Velocity>() {
    entity.position += entity.velocity
}
```

**Implementação:**

`@archetype` faz o checker gerar:

1. Um `NVArray` layout-contíguo para cada componente (SoA — Structure of Arrays)
2. Funções de query especializadas `__query_Position_Velocity(world: World): ArchetypeView`
3. Iterador sem virtual dispatch

`query<T1, T2>()` é resolvido em compile-time: o checker busca archetypes que contêm
todos os tipos pedidos e gera chamada direta ao `__query_...` correspondente.

Layout SoA gerado:

```c
// Para Archetype(Position, Velocity):
struct __archetype_Movement {
    Position* positions;    // contíguo
    Velocity* velocities;   // contíguo
    int count;
};
```

Isso maximiza cache locality e elimina pointer indirection por entity.

### 5.11 Parser Generators em Compile-Time

```narval
comptime grammar {
    expr   = term (('+' | '-') term)*
    term   = factor (('*' | '/') factor)*
    factor = NUMBER | '(' expr ')'
}
```

O bloco `grammar { }` é um DSL (via macro `grammar!`) que:

1. Parseia a BNF em compile-time
2. Gera um parser LL(1) ou PEG como conjunto de `def`s Narval
3. Injeta as funções no namespace atual

Funções geradas:
- `parse_expr(tokens): ExprNode`
- `parse_term(tokens): TermNode`
- etc.

Tabelas de parse geradas como arrays `comptime` — zero overhead de inicialização.

### 5.12 GPU Kernel Generation

```narval
@gpu(target: "cuda")
comptime def add_kernel(a: Tensor<float, [-1]>, b: Tensor<float, [-1]>): Tensor<float, [-1]> {
    i = thread.idx.x
    return a[i] + b[i]
}
```

**Implementação (fase futura / extensível):**

O atributo `@gpu(target: "cuda")` instrui o codegen a:

1. Gerar IR NVPTX em vez de x86 para essa função
2. Gerar wrapper de launch: `__launch_add_kernel(grid, block, a, b)`
3. Gerar fallback CPU via `@vectorize` automaticamente

O compilador invoca `llc --march=nvptx64` no IR gerado e embute o PTX como string no
binário (`__ptx_add_kernel`). O runtime carrega via `nvrtcCompileProgram` ou `cuModuleLoadData`.

Shapes estáticos (`Tensor<float, [1024]>`) permitem geração de launch com grid/block
calculados em compile-time:

```narval
@gpu(target: "cuda")
comptime def add_1024(a: Tensor<float, [1024]>, ...): ... {
    # grid = 4, block = 256 calculados automaticamente
}
```

---

## 6. Sistema de Tipos — Extensões

### 6.1 Novos Kinds

```cpp
enum Kind {
    // ... existentes ...
    TENSOR,      // Tensor<T, [M, N, ...]>
    COMPTIME,    // tipo de uma função/variável comptime (checker-only, não chega ao codegen)
};
```

### 6.2 `ComptimeType`

```cpp
struct ComptimeType : public Type {
    std::shared_ptr<Type> inner;   // tipo do valor comptime subjacente
    ComptimeValue constant_value;  // valor conhecido em compile-time

    ComptimeType(std::shared_ptr<Type> inner, ComptimeValue val)
        : Type(Kind::COMPTIME), inner(std::move(inner)),
          constant_value(std::move(val)) {}

    std::string toString() override { return "comptime " + inner->toString(); }
};
```

`ComptimeType` é transparente para o type-checker: unifica com o `inner` type normalmente.
É usado apenas para propagar o valor constante e permitir que expressões que dependem
de variáveis comptime sejam avaliadas.

### 6.3 Regras de Subtipagem

- `comptime T` é subtipo de `T` (pode ser usado onde `T` é esperado)
- `T` não é subtipo de `comptime T` (não pode passar runtime value para parâmetro comptime)
- Parâmetros comptime de funções especializadas: `def foo<comptime N: int>(...)`

---

## 7. Mensagens de Erro

Erros comptime devem indicar claramente que ocorreram em compile-time:

```
error[CE001]: compile-time evaluation failed
  → test.nv:12:5
  |
12 |   comptime x = fib(-1)
  |   ^^^^^^^^^^^^^^^^^^^^
  |
  = note: stack overflow at depth 1024 in comptime function 'fib'
  = note: called from: fib(0) → fib(-1) → fib(-2) → ...

error[CE002]: shape mismatch in compile-time tensor operation
  → test.nv:8:14
  |
8  |   c = matmul(a, b)
  |              ^^^^^^
  |
  = note: left operand shape:  [32, 128]
  = note: right operand shape: [64, 64]
  = note: inner dimensions must match (128 ≠ 64)

error[CE003]: non-comptime value used in comptime context
  → test.nv:5:20
  |
5  |   comptime for i in n..100 {
  |                      ^
  |
  = note: 'n' is a runtime variable; comptime range must be fully known at compile-time
```

---

## 8. Ordem de Implementação (Roadmap)

### Fase 1 — Fundação (bloqueante para o resto)
1. Token `COMPTIME` no lexer
2. `ComptimeValue` struct + `ComptimeEvaluator` básico (int, float, bool, str, aritmética)
3. `comptime x = <literal>` — variável comptime simples
4. `comptime def` + chamada de função comptime simples (sem recursão)
5. `comptime if` — branch removal

### Fase 2 — Loops e Tipos
6. `comptime for` com range estático → unrolling
7. `type.fields(T)` e `type.name(T)` no evaluator
8. `@derive(debug, eq, clone)` — os mais simples

### Fase 3 — Geração de Código Avançada
9. `@derive(json, hash, sql)`
10. Tabelas comptime → globals `.rodata` no codegen
11. `comptime import_c(...)` com parser de headers C

### Fase 4 — Tensores e SIMD
12. `TensorType` + shape inference em compile-time
13. `@vectorize` com LLVM metadata
14. `comptime if SIMD == "avx2" { }` via `target.*` built-ins

### Fase 5 — DSLs e Macros
15. `MacroCallNode` no parser + dispatch para `comptime def name!(src)`
16. `sql!`, `regex!`, `route!` como biblioteca padrão

### Fase 6 — Auto-diff e ECS
17. `type.ast(f)` para reflexão de AST
18. `autodiff` como função comptime de biblioteca
19. `@archetype` + `query<...>()` especializado

### Fase 7 — GPU (Fase Futura)
20. `@gpu(target: "cuda")` + geração de PTX via NVPTX backend do LLVM

---

## 9. Restrições e Limitações Conhecidas

- Funções comptime não podem chamar funções de I/O runtime (`write`, `read`, etc.)
- `comptime import_c` não suporta headers com macros C complexas (apenas declarações)
- Auto-diff suporta apenas funções de variável única inicialmente
- `@gpu` requer LLVM compilado com suporte a NVPTX (`-DLLVM_TARGETS_TO_BUILD="NVPTX;X86"`)
- Arrays comptime muito grandes (>1M elementos) podem aumentar tempo de compilação significativamente
- Funções comptime não têm acesso ao filesystem (exceto via `comptime import_c`)
- `type.ast(f)` funciona apenas para `comptime def` e funções marcadas com `@comptime_visible`
