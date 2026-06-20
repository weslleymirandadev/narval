# Narval Ownership & Borrowing System — Design Document

> **Status:** Design phase — guia para implementação incremental.  
> **Filosofia central:** O compilador carrega a responsabilidade, não o programador.

---

## 0. Invariante Inquebrável — Leia Antes de Qualquer Coisa

> **O programador Narval nunca deve pensar em ownership, borrow, lifetime ou move.**  
> **Nunca. Nenhuma anotação. Nenhum modelo mental novo. Nenhum erro de borrow na cara do usuário.**

Esta não é uma preferência de design — é uma **restrição dura**. Toda decisão de implementação deve ser avaliada contra ela. Se uma feature exige que o programador entenda ownership para usá-la corretamente, a feature está errada, não o programador.

### O que isso significa concretamente

O código Narval deve parecer Python ou JavaScript em termos de modelo mental:

```python
# O programador escreve isso — sem pensar em nada além da lógica
x = load_data("file.csv")
processed = transform(x)
save(processed)
```

Por baixo dos panos, o compilador resolve tudo:
- `x` é owned; quando `transform(x)` é o último uso de `x`, o compilador move automaticamente
- `processed` recebe ownership do retorno de `transform`
- `save(processed)` move `processed` (último uso) — sem cópia, sem overhead
- Tudo é destruído na ordem certa, sem GC, sem pause, sem leak

O programador não viu `move`, `&`, `'a`, `mut`, `Box`, `Rc`, `Arc`. Nada.

### Hierarquia de decisões do compilador (em ordem)

Quando o compilador analisa um valor, tenta em sequência:

1. **Copy** — Todos os dados do tipo vivem na stack e copiar bit a bit é correto e barato? Copia trivialmente, sem custo. Isso é inferido **estruturalmente**: `int`, `float`, `bool` são Copy; `Option<int>` é Copy (porque `int` é); `Option<Vec>` não é (porque `Vec` possui heap). O compilador infere isso automaticamente — nenhuma anotação.
2. **Move** — É o último uso deste binding? Transfere ownership, sem cópia, sem refcount.
3. **Borrow** — O valor ainda é usado depois? Cria referência temporária, verifica que não escapa.
4. **ARC silencioso** — Aliasing complexo que o checker não resolve? Usa refcount atômico, transparentemente.

O programador nunca escolhe entre essas opções. O compilador escolhe. Sempre.

### O que NUNCA deve existir na surface language

- Anotações de lifetime (`'a`, `'static`)
- Operadores de borrow explícito (`&x`, `&mut x`)
- Operadores de move explícito (`move(x)`, `std::move(x)`)
- Smart pointers expostos (`Box<T>`, `Rc<T>`, `Arc<T>`)
- Erros de "valor já foi movido" visíveis ao programador
- Qualquer conceito que force o programador a raciocinar sobre memória

### Quando o compilador não consegue provar

Se o compilador não consegue determinar estaticamente a estratégia correta → **ARC silencioso**. Nunca um erro de ownership. O programador pode usar `--arc-stats` para ver onde o compilador recorreu ao ARC (como hint de otimização, não como erro).

---

## 1. Estado Atual e Motivação

### O que existe hoje

Narval usa **reference counting manual** no runtime C:

```c
typedef struct NvObject {
    struct NvTypeObject* ob_type;
    int32_t ref_count;       // contagem manual, nunca decrementada no codegen
    uint32_t flags;
} NvObject;
```

Problemas concretos:
- `nv_incref`/`nv_decref` existem mas **nunca são emitidos** pelo codegen (apenas nas funções de criação)
- Cada `create_int`, `create_str`, etc. faz `malloc` + inicia `ref_count = 1`
- Ao reassignar uma variável, o valor anterior nunca é `decref`'d → **memory leak**
- Não há verificação estática de aliasing, lifetime ou mutation exclusiva
- Sem move semantics: tudo é cópia implícita do `Value` struct (ponteiro de 8 bytes)

### Por que não GC

GC pausas afetam latência (inaceitável para ML/tensores), GC torna tempo de vida de objetos imprevisível, e a proposta do Narval é **performance previsível com safety automática** — exatamente o oposto do GC.

### Por que não Rust 1:1

Rust expõe o borrow checker ao programador. Narval quer que o **compilador resolva silenciosamente** o que Rust exige que o programador anote. A estratégia é:

1. Inferir ownership no caso comum (análise de último uso)
2. Inferir lifetimes automaticamente (algoritmo baseado em escopo)
3. Fallback para ARC (Automatic Reference Counting) quando a análise não for conclusiva
4. Nunca forçar o usuário a anotar `&`, `'a`, `mut` explicitamente

---

## 2. Modelo de Ownership (interno ao compilador)

> Estes são conceitos **internos do compilador**. O programador nunca os vê, nunca os nomeia, nunca precisa entendê-los. São a linguagem que o compilador usa internamente para raciocinar sobre memória.

### Quatro modos (invisíveis ao programador)

```
Copy     → tipo cujos dados são inteiramente stack-resident e fixed-size
           o compilador infere isso estruturalmente:
           - int, float, bool              → sempre Copy
           - Option<int>                   → Copy (int é Copy)
           - Option<Vec>                   → NÃO Copy (Vec possui heap)
           - Result<int, bool>             → Copy (ambos são Copy)
           - Result<String, Error>         → NÃO Copy (String possui heap)
           - Tensor                        → NUNCA Copy (heap potencialmente enorme)

Owned    → uma variável possui o objeto; destruído automaticamente ao sair do escopo
Borrowed → referência temporária interna; verificada pelo compilador
Shared   → ARC automático quando o compilador não consegue provar posse única
```

### Como o compilador decide (sem interação do programador)

```python
# Código que o programador escreve:
x = load("data.csv")   # x: Owned<Data>
process(x)             # último uso de x → compilador emite move(x), não cópia
# x destruído automaticamente dentro de process()

# Outro exemplo:
items = [1, 2, 3]
first = items[0]       # items ainda é usado depois → compilador emite borrow
print(items)           # items ainda vive aqui

# Aliasing:
a = create()
b = a                  # a ainda é usado depois de b → compilador usa ARC silencioso
use(a)
use(b)                 # ambos válidos, ARC cuida da destruição
```

### Casos especiais futuros

**`Tensor`** — Nunca Copy, nunca ARC por padrão. Tensores podem ter gigabytes; o custo de um `ref_inc`/`ref_dec` atômico por operação seria proibitivo em loops de ML. A estratégia:
- Passar tensor para função → Move (transferência de ponteiro, zero-copy)
- Indexar/slicear tensor → Borrow interno (view sem cópia, sem contagem)
- Quando aliasing genuinamente necessário → Clone explícito **gerado pelo compilador** (o programador só vê `b = a`, o compilador decide se clona ou faz ARC baseado no padrão de uso)

**`Option<T>` e `Result<T, E>`** — Copy se e somente se `T` (e `E`) for Copy. O compilador resolve isso recursivamente na análise de tipos. O programador nunca precisa saber.

### O compilador nunca emite erro de ownership visível

Se o programador escreve código que parece correto em termos de lógica, o compilador **encontra uma estratégia** (Copy → Move → Borrow → ARC) e a aplica. Nunca joga a responsabilidade de volta para o programador.

A única exceção possível no futuro: uso explícito após destruição **provadamente impossível de resolver** (ex: usar variável que foi enviada para outra thread que a destruiu). Mesmo assim, o erro seria em termos de **semântica do programa** ("esta variável não existe mais neste contexto"), não em termos de ownership.

---

## 3. NIR — Narval Intermediate Representation

Entre o AST tipado e o LLVM IR, inserimos uma nova camada chamada **NIR**. Esta é a peça central do design.

### Por que um IR próprio

1. Permite expressar ownership explicitamente (sem poluir o AST)
2. Desacopla análise de ownership da geração LLVM
3. Abre caminho para lowering alternativo para **MLIR** (tensores, ML)
4. Torna otimizações de ownership (elision de copies, stack promotion) modulares

### Estrutura do NIR

```
NIRModule
├── NIRFunction (nome, params com ownership, ret com ownership)
│   └── NIRBlock (básico block, lista de NIRInst)
│       ├── NIRInst::Alloc      (alocar owned value)
│       ├── NIRInst::Move       (transferir ownership)
│       ├── NIRInst::Borrow     (criar referência temporária)
│       ├── NIRInst::Drop       (destruir owned value)
│       ├── NIRInst::Clone      (deep copy para Shared)
│       ├── NIRInst::RefInc     (ARC increment)
│       ├── NIRInst::RefDec     (ARC decrement + free se 0)
│       ├── NIRInst::Region     (marca escopo de lifetime)
│       ├── NIRInst::Call       (call com ownership dos args)
│       ├── NIRInst::Return     (return com ownership)
│       └── NIRInst::Tensor     (operação tensor — MLIR futuro)
```

### Tipos NIR

```cpp
// Ownership mode em todo valor NIR
enum class OwnershipKind {
    Owned,      // dono único, será dropped no fim do escopo
    Borrowed,   // referência com lifetime limitado
    Shared,     // ARC, múltiplos donos
    Moved,      // já foi transferido (binding morto)
    Copy,       // tipos primitivos simples: int, float, bool (Copy semantics)
};

// Lifetime: escopo nomeado (como 'a em Rust, mas gerado pelo compilador)
struct Lifetime {
    uint32_t id;        // gerado pelo compilador
    uint32_t scope_depth;
};

// Tipo NIR: combina tipo narval + ownership
struct NIRType {
    std::shared_ptr<Type> narval_type;   // tipo do checker (Int, Str, Class, etc.)
    OwnershipKind ownership;
    std::optional<Lifetime> lifetime;    // presente quando Borrowed
};
```

### Instruções NIR chave

```
; Alocar valor owned
%x : Owned<Int> = nir.alloc Int, 42

; Mover ownership (x fica Moved após isso)
%y : Owned<Int> = nir.move %x

; Criar borrow com lifetime 'L0
%r : Borrowed<'L0, Int> = nir.borrow %y, lifetime='L0

; Drop explícito (será inserido automaticamente no fim do escopo)
nir.drop %y

; Chamada com ownership: arg0 é moved, arg1 é borrowed
%result : Owned<Str> = nir.call @foo, move(%a), borrow(%b)

; ARC
%s : Shared<Vec> = nir.clone %original
nir.ref_inc %s
nir.ref_dec %s        ; free se count == 0

; Tensor (futuro MLIR)
%t : Owned<Tensor<f32, [128, 256]>> = nir.tensor.alloc [128, 256]
%u : Owned<Tensor<f32, [128, 256]>> = nir.tensor.matmul %t, %w
```

---

## 4. Algoritmo de Inferência de Ownership

### Passo 1: Last-use analysis (análise de último uso)

Para cada binding `x`, rastrear todos os pontos de uso no CFG. O **último uso** de `x` em qualquer caminho de execução é um **move** (a menos que seja uma borrow explícita).

```
x = create()          ; Owned
foo(x)                ; uso intermediário → borrow (se foo não escapa x)
bar(x)                ; ÚLTIMO uso → move
; x agora é Moved — qualquer uso após isso é erro estático
```

Implementação: dataflow analysis (análise de fluxo de dados) backward no CFG:
- `live_out[B]` = union de `live_in` de sucessores de B
- `live_in[B]` = `use[B]` ∪ (`live_out[B]` − `def[B]`)

### Passo 2: Escape analysis

Detectar quando um valor "escapa" o escopo onde foi criado:
- Retornado de uma função → o valor é moved para o caller
- Armazenado em estrutura de vida mais longa → o valor é moved para essa estrutura
- Capturado por closure → o valor é moved para o ambiente da closure

Implementação: interprocedural escape analysis baseada em anotações do tipo de retorno e parâmetros de função.

### Passo 3: Alias analysis

Detectar quando duas variáveis apontam para o mesmo objeto:
- `y = x` → y e x são aliases → ao mutar um, o outro é afetado
- Se ambos precisam sobreviver simultaneamente: promover para Shared (ARC)
- Se não: um é owner, outro é borrow com lifetime do owner

### Passo 4: Resolução de conflitos

| Situação detectada | Estratégia |
|---------------------|------------|
| Único uso claro      | Owned, move no último uso |
| Borrow simples       | Borrowed, lifetime = escopo do caller |
| Aliasing sem escape  | Borrowed para o alias |
| Aliasing com escape  | Promover para Shared (ARC) |
| Closure que captura  | Move para ambiente da closure |
| Inconclusivo         | Shared (ARC conservador) |

---

## 5. Borrow Checker

O borrow checker é um **passo separado** no pipeline, após o type checker e antes da geração NIR.

### Regras do borrow checker Narval

**Regra 1 — Exclusividade de mutação:**
Se existe borrow ativo de `x`, `x` não pode ser mutado nem moved.

**Regra 2 — Lifetime de borrows:**
Um borrow `&x` com lifetime `'L` não pode outlive o escopo onde `x` foi alocado.

**Regra 3 — Movido não pode ser usado:**
Após `move(x)`, qualquer uso de `x` é erro estático.

**Regra 4 — Inferência agressiva (diferente de Rust):**
Quando as regras 1-3 não puderem ser provadas estaticamente, o compilador **não emite erro** — ele promove para Shared (ARC) automaticamente.

### Estrutura do checker

```
BorrowChecker
├── build_cfg(FunctionAST) → CFG
├── compute_liveness(CFG) → LivenessInfo
├── analyze_escapes(CFG) → EscapeInfo
├── check_borrows(CFG, LivenessInfo, EscapeInfo) → BorrowErrors + OwnershipMap
└── annotate_ast(AST, OwnershipMap) → AnnotatedAST
```

### Quando usar ARC vs. tentar provar estaticamente

O borrow checker do Narval **nunca emite erros de ownership ao programador**. A decisão é sempre:

- Consegue provar posse única e uso sequencial? → Move (zero overhead)
- Consegue provar que o uso posterior é só leitura temporária? → Borrow interno
- Não consegue provar nada disso? → **ARC silencioso** (seguro, um pouco mais lento)

O programador pode rodar `narval --arc-stats file.nv` para ver onde o compilador usou ARC como hint de otimização. Mas nunca como erro, nunca como obrigação de mudar o código.

---

## 6. Plano de Implementação por Fases

### Fase 0: Preparação da infraestrutura (Sem mudar semântica)

**Objetivo:** Adicionar scaffolding sem quebrar o compilador existente.

**Arquivos a criar/modificar:**

```
include/frontend/ownership/
├── ownership_kind.hpp       (enum OwnershipKind, struct Lifetime)
├── ownership_map.hpp        (OwnershipMap: Node* → OwnershipKind)
└── borrow_checker.hpp       (classe BorrowChecker)

include/backend/nir/
├── nir_types.hpp            (NIRType, NIRInst enum)
├── nir_inst.hpp             (todas as instruções NIR como structs)
├── nir_block.hpp            (NIRBlock: lista de instruções)
├── nir_function.hpp         (NIRFunction: params, blocks)
└── nir_module.hpp           (NIRModule: funções + globals)

src/frontend/ownership/
├── borrow_checker.cpp       (implementação do BorrowChecker)
├── escape_analysis.cpp      (escape analysis)
└── liveness.cpp             (dataflow liveness)

src/backend/nir/
├── ast_to_nir.cpp           (lowering do AnnotatedAST para NIR)
├── nir_to_llvm.cpp          (lowering do NIR para LLVM IR)
└── nir_printer.cpp          (debug printer para NIR textual)
```

**Modificações em arquivos existentes:**

- `include/frontend/ast/types.hpp`: adicionar campo `OwnershipKind ownership_hint` em `Node`
- `include/frontend/checker/type.hpp`: adicionar campo `OwnershipKind ownership` em `Type`
- `src/main.cpp`: adicionar `BorrowChecker` passo após `Checker`, antes de `generate_ir`

**Output desta fase:** O compilador compila igual que antes. O BorrowChecker roda mas não emite erros — apenas popula o `OwnershipMap` com análise básica.

---

### Fase 1: Copy types e Drop automático para primitivos

**Objetivo:** Implementar o caso mais simples: `int`, `float`, `bool` têm **copy semantics** (sem overhead). Outros tipos recebem **drop** automático no fim do escopo.

**Mudanças no NIR lowering:**

```cpp
// ast_to_nir.cpp
NIRInst lower_declaration(DeclarationStmtNode* node, OwnershipMap& map) {
    auto kind = map.get(node);
    if (is_copy_type(node->type)) {
        return NIRInst::Alloc{node->name, OwnershipKind::Copy, node->init};
    }
    return NIRInst::Alloc{node->name, OwnershipKind::Owned, node->init};
}

// No fim de cada scope, inserir drops para Owned values:
void insert_scope_drops(NIRBlock& block, ScopeInfo& scope) {
    for (auto& [name, info] : scope.owned_values) {
        block.append(NIRInst::Drop{info.nir_value});
    }
}
```

**Mudanças no runtime:**

- Adicionar `nv_free_owned(Value* v)` em `nv_runtime.h`: chama `tp_dealloc` + zera o ponteiro
- No `nir_to_llvm.cpp`: `NIRInst::Drop` → chamada para `nv_free_owned`

**Testes:** Verificar que programas simples com variáveis locais não vazam memória (usar valgrind ou AddressSanitizer).

---

### Fase 2: Move semantics em assignments e chamadas de função

**Objetivo:** Inferir quando um valor é movido (último uso) vs. emprestado.

**Last-use analysis:**

```cpp
// liveness.cpp
class LivenessAnalysis {
    // Para cada ponto P no CFG e variável x:
    // live(P, x) = true se x é usado em algum caminho de P ao fim da função
    
    LivenessMap compute(CFGNode* cfg);
};

// borrow_checker.cpp
OwnershipMap BorrowChecker::analyze(FunctionAST* fn) {
    auto cfg = build_cfg(fn);
    auto liveness = LivenessAnalysis().compute(cfg);
    OwnershipMap map;
    
    for (auto& use : all_uses(fn)) {
        if (!liveness.is_live_after(use)) {
            // Este é o último uso: move
            map.set(use, OwnershipKind::Moved);
        } else {
            // Há uso posterior: borrow
            map.set(use, OwnershipKind::Borrowed);
        }
    }
    return map;
}
```

**NIR para assignment:**

```
; y = x  (x não é mais usado depois)
%x_moved : Moved<T> = nir.move %x
%y : Owned<T> = %x_moved
; %x agora está Moved — qualquer uso posterior é erro

; y = x  (x ainda será usado depois — borrow implícito)
%x_ref : Borrowed<'L0, T> = nir.borrow %x
%y = nir.clone %x_ref   ; copia superficial para y
```

**NIR para chamadas:**

```
; foo(x) onde x não é usado depois → move
nir.call @foo, move(%x)

; foo(x) onde x é usado depois → borrow
nir.call @foo, borrow(%x)
```

**Mudanças no runtime C:**

- `nv_move(Value* src, Value* dst)`: transfere sem incref (ownership transferida)
- `nv_borrow(Value* src)`: retorna ponteiro bruto sem incref (borrow temporário)

---

### Fase 3: Lifetime inference para borrows

**Objetivo:** Garantir que borrows não outlive seus donos.

**Representação de lifetimes:**

```cpp
struct Lifetime {
    uint32_t id;
    uint32_t scope_depth;   // profundidade do escopo do dono
    
    bool outlives(const Lifetime& other) const {
        return scope_depth <= other.scope_depth;   // dono em escopo mais externo
    }
};
```

**Algoritmo de atribuição de lifetimes:**

1. Cada `Owned` value recebe lifetime `'Ln` onde `n` = profundidade do escopo
2. Cada `Borrowed` value recebe lifetime `'Lk` onde `k >= n` (borrow dentro do escopo do dono)
3. Verificar: se um `Borrowed` com lifetime `'Lk` tenta escapar para escopo `'Lm < 'Lk`, é erro (ou promoção para Shared)

**Verificação:**

```cpp
void check_lifetime(NIRFunction& fn) {
    for (auto& inst : fn.all_insts()) {
        if (auto* ret = std::get_if<NIRInst::Return>(&inst)) {
            if (ret->value.ownership == OwnershipKind::Borrowed) {
                // Borrow escapando pela return → erro ou promoção para Owned (clone)
                if (!can_promote_to_owned(ret->value)) {
                    emit_error("borrow escaping function scope");
                } else {
                    promote_to_clone(ret->value);  // insere nir.clone antes do return
                }
            }
        }
    }
}
```

---

### Fase 4: Shared (ARC) automático para casos complexos

**Objetivo:** Quando aliasing ou escape não puder ser resolvido estaticamente, promover para ARC.

**Detecção de aliasing:**

```cpp
bool needs_arc(Value* v, EscapeInfo& escapes) {
    return escapes.has_multiple_owners(v) ||
           escapes.escapes_scope(v) ||
           escapes.captured_by_closure(v);
}
```

**NIR para Shared:**

```
; Quando ARC é necessário
%shared : Shared<T> = nir.clone %original    ; ref_count começa em 1
nir.ref_inc %shared                          ; ao copiar para outro binding
nir.ref_dec %shared                          ; ao fim de escopo de cada binding
```

**No runtime C:**

- Mudar `ref_count` para `_Atomic int32_t` (thread-safe para ARC)
- `nv_arc_inc(NvObject*)` / `nv_arc_dec(NvObject*)` com operações atômicas

---

### Fase 5: Integração no pipeline principal

**Modificar `src/main.cpp`:**

```cpp
// Pipeline atual:
// Lexer → Parser → Checker → generate_ir → LLVM

// Novo pipeline:
// Lexer → Parser → Checker → BorrowChecker → ASTtoNIR → NIR opts → NIRtoLLVM → LLVM

int compile(const std::string& filename) {
    // ... lexer, parser, checker ...
    
    // NOVO: borrow checking + NIR
    BorrowChecker borrow_checker;
    auto ownership_map = borrow_checker.analyze(ast);
    
    NIRModule nir = lower_ast_to_nir(ast, ownership_map);
    optimize_nir(nir);          // elision de copies, stack promotion
    
    // Substitui generate_ir:
    lower_nir_to_llvm(nir, llvm_context);
    
    // ... emissão de objeto, linkagem ...
}
```

---

### Fase 6: MLIR para tensores e ML

**Objetivo:** Usar NIR como ponto de entrada para operações de tensor, gerando MLIR em vez de LLVM para kernels numéricos.

**Tipo Tensor no NIR:**

```
%t : Owned<Tensor<f32, [batch, 128, 256]>> = nir.tensor.alloc [batch, 128, 256]
%w : Owned<Tensor<f32, [256, 64]>> = nir.tensor.load "weights.bin"
%out : Owned<Tensor<f32, [batch, 128, 64]>> = nir.tensor.matmul %t, %w
nir.drop %t
nir.drop %w
```

**Lowering para MLIR:**

```cpp
// nir_to_mlir.cpp (futuro)
mlir::Value lower_tensor_matmul(NIRInst::TensorMatmul& inst, mlir::OpBuilder& builder) {
    // Emite linalg.matmul no dialeto MLIR
    return builder.create<mlir::linalg::MatmulOp>(...);
}
```

**Ownership de tensores:**

- Tensores são sempre `Owned` (nunca ARC — dados grandes)
- Move semântica para tensores = zero-copy pointer transfer
- Borrow de tensores = readonly view (slice, sem cópia)
- Reshape/view = novo `Borrowed` tensor com mesmo storage

---

## 7. Mudanças no Runtime C

### `include/backend/runtime/prototypes.h`

```c
// Mudar ref_count para atomic (para ARC thread-safe)
typedef struct NvObject {
    struct NvTypeObject* ob_type;
    _Atomic int32_t ref_count;   // era: int32_t
    uint32_t flags;
    uint8_t ownership;           // NOVO: 0=owned, 1=shared, 2=borrowed
} NvObject;
```

### Novas funções em `nv_runtime.h`

```c
// Ownership primitives
void nv_drop_owned(Value* v);           // destrói valor owned (no decref check)
Value* nv_move(Value* src);             // transfere, zera src
Value* nv_borrow(const Value* src);     // ponteiro temporário, sem incref
Value* nv_clone_shared(Value* src);     // incref + retorna mesmo ponteiro
void nv_arc_inc(NvObject* obj);         // atomic increment
void nv_arc_dec(NvObject* obj);         // atomic decrement, free se 0
```

---

## 8. Novas Flags de Compilação

```
narval --ownership-debug file.nv    # print NIR com anotações de ownership
narval --no-arc file.nv             # desabilita ARC, erro em casos ambíguos
narval --arc-stats file.nv          # reporta onde ARC foi usado (hints de otimização)
narval --emit-nir file.nv           # imprime NIR em vez de compilar
narval --mlir-tensors file.nv       # usa MLIR para operações de tensor (futuro)
```

---

## 9. Checklist de Implementação para Agentes de IA

Use esta checklist em ordem. Cada item é um PR separado.

### Infraestrutura NIR (Fase 0)
- [ ] Criar `include/frontend/ownership/ownership_kind.hpp`
- [ ] Criar `include/backend/nir/nir_types.hpp`
- [ ] Criar `include/backend/nir/nir_inst.hpp`
- [ ] Criar `include/backend/nir/nir_block.hpp`
- [ ] Criar `include/backend/nir/nir_function.hpp`
- [ ] Criar `include/backend/nir/nir_module.hpp`
- [ ] Criar `src/backend/nir/nir_printer.cpp` (debug)
- [ ] Adicionar `ownership_hint` em `Node` em `types.hpp`
- [ ] Adicionar `ownership` em `Type` em `type.hpp`
- [ ] Criar `BorrowChecker` stub em pipeline (não faz nada ainda)
- [ ] Atualizar CMakeLists.txt para compilar os novos arquivos

### Drop automático (Fase 1)
- [ ] Implementar `ast_to_nir.cpp` básico (apenas Alloc + Drop no fim do escopo)
- [ ] Implementar `nir_to_llvm.cpp` básico (apenas Alloc → alloca, Drop → nv_drop_owned)
- [ ] Adicionar `nv_drop_owned` no runtime C
- [ ] Testar: variáveis locais são dropadas ao sair do escopo
- [ ] Testar com valgrind: sem memory leaks em programas simples

### Move semantics (Fase 2)
- [ ] Implementar `liveness.cpp` (dataflow analysis)
- [ ] Implementar `borrow_checker.cpp` (last-use → move, outros → borrow)
- [ ] Adicionar `nv_move` e `nv_borrow` no runtime C
- [ ] `ast_to_nir.cpp`: usar OwnershipMap para emitir Move vs. Borrow em assignments
- [ ] `ast_to_nir.cpp`: usar OwnershipMap para emitir Move vs. Borrow em call args
- [ ] Testar: variáveis não são usadas após move (erro estático)

### Lifetime inference (Fase 3)
- [ ] Implementar `escape_analysis.cpp`
- [ ] Adicionar lifetime IDs em NIRType
- [ ] Implementar verificação de borrows que outlive owners
- [ ] Promoção automática: borrow que escapa → clone
- [ ] Testar: return de borrow → erro ou auto-clone

### ARC (Fase 4)
- [ ] Mudar `ref_count` para `_Atomic int32_t` no runtime
- [ ] Implementar `nv_arc_inc` / `nv_arc_dec` com atomic ops
- [ ] Implementar detecção de aliasing no BorrowChecker
- [ ] `ast_to_nir.cpp`: promover para Shared quando aliasing detectado
- [ ] Testar: programas com aliasing rodam sem crashes
- [ ] Testar: sem memory leaks com ARC

### Integração no pipeline (Fase 5)
- [ ] Substituir `generate_ir` por `BorrowChecker → ASTtoNIR → NIRtoLLVM`
- [ ] Implementar `--emit-nir` flag
- [ ] Implementar `--ownership-debug` flag
- [ ] Rodar todos os testes existentes com novo pipeline
- [ ] Performance baseline: comparar tempo de execução antes/depois

### Tensores/MLIR (Fase 6)
- [ ] Adicionar `NIRInst::Tensor*` instruções
- [ ] Implementar `nir_to_mlir.cpp`
- [ ] Integrar MLIR como dependência opcional no CMakeLists.txt
- [ ] Implementar `--mlir-tensors` flag
- [ ] Testar matmul básico via MLIR

---

## 10. Arquivos Críticos para Referência

| Arquivo | Relevância |
|---------|------------|
| `include/frontend/ast/types.hpp` | Onde adicionar `ownership_hint` em Node |
| `include/frontend/checker/type.hpp` | Onde adicionar `ownership` em Type |
| `include/backend/codegen/ir_context.hpp` | SymbolInfo: adicionar ownership tracking |
| `src/backend/codegen/generate_ir.cpp` | Ponto de entrada do backend (será substituído por NIR) |
| `src/backend/codegen/statements/generate_declaration_stmt.cpp` | Onde ownership de variáveis locais começa |
| `src/backend/codegen/statements/generate_def_stmt.cpp` | Onde ownership de params de função é definido |
| `src/backend/codegen/expressions/generate_assignment_expr.cpp` | Onde move vs. copy é decidido |
| `src/backend/codegen/expressions/generate_call_expr.cpp` | Onde ownership dos argumentos é decidido |
| `include/backend/runtime/prototypes.h` | Estrutura NvObject (adicionar `_Atomic`, `ownership` flag) |
| `src/backend/runtime/value_creation.c` | Onde objetos são alocados (base para nv_drop_owned) |
| `src/main.cpp` | Pipeline principal (onde inserir BorrowChecker e NIR) |

---

## 11. Referências de Design

- **Swift ARC**: modelo de ARC silencioso para casos complexos
- **Nim ORC**: ownership com cycle detection automático (inspiração para aliasing)
- **Lobster language**: ownership inferido sem anotações (mais próximo da visão Narval)
- **MLIR linalg dialect**: dialeto para operações lineares (destino futuro para tensores)
- **Perceus (Koka)**: reference counting funcional com elision de copies em estilo FP
- **Vale language**: ownership implícito em linguagem de alto nível

---

*Documento criado em 2026-05-01. Atualizar conforme implementação progride.*
