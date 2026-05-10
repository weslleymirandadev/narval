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
