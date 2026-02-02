# Narval

Linguagem de programação compilada multiparadigma de alto desempenho com tipagem inferida que usa o conceito de Ownership & Borrowing implícito e inferido (SIM, sem anotações explícitas, sem erros de borrow na cara do usuário). O compilador assume a responsabilidade por memória, paralelismo e segurança — sem impor um novo modelo mental ao programador.

Narval transfere a responsabilidade operacional do código para o compilador, mantendo previsibilidade, performance e controle quando necessário. Se é possível provar que é seguro, Narval faz automaticamente. Diferente de Rust, Narval não tenta ensinar o programador a escrever código correto, ela tenta fazer código comum se comportar como código expert.

# Features da linguagem

- Compilada
- Tipagem inferida
- Multiparadigma
- Paralelismo massivo
- Interoperabilidade com C/C++, Assembly, Rust, Python e linguagens LLVM-based
- Tooling completo
- Modo REPL
- Modo Notebooks (inspirado em Python)
- Frontend própio
- Backend LLVM
- Geração de código altamente otimizada
- Sem Garbage Collector
- Ownership & Borrowing (inspirado em Rust)
  - Sem anotações explícitas
  - Sem erros de borrow na cara do usuário
  - Infere ownership
  - Decide move/borrow/clone
  - Gerencia lifetime automaticamente
  - Ownership explicável com flag `--explain-ownership` no build
- Sharp edges explícitas
  - `@unsafe`
  - `@trust_me`
  - `@no_infer`
  - Escape hatches conscientes
  - Segurança sem "magia infinita"
