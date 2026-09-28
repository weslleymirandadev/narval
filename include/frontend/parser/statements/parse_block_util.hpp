#pragma once
#include "frontend/parser/parser.hpp"
#include "frontend/ast/types.hpp"
#include <vector>
#include <memory>

// Faz o parse de uma lista de statements até encontrar CBRACE ou EOF.
// Quando encontra um DeferErrorStatement, coleta todos os statements restantes
// como o `remaining_body` do nó defer, e encerra o loop.
// NÃO consome as chaves — caller deve consumir OBRACE antes e CBRACE depois.
std::vector<std::unique_ptr<Stmt>> parse_body(Parser* parser);

// O corpo de um `if`/`elif`/`else`/`while`/`for`: ou o bloco entre chaves de sempre, ou — para
// uma linha só — a forma inline com dois-pontos, `if cond: return x;`. O `:` é açúcar para o
// caso mais comum (guarda, early return) e evita a chave de uma linha. Consome o `{ }` ou a
// statement única.
std::vector<std::unique_ptr<Stmt>> parse_block_or_inline(Parser* parser);
