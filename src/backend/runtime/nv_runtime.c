#include "backend/runtime/nv_runtime.h"
#include "backend/runtime/prototypes.h"
#include <stdio.h>
#include <string.h>

extern StringVTable string_vtable_instance;
extern ArrayVTable  array_vtable_instance;
extern VectorVTable vector_vtable;
extern MapVTable    map_vtable;

void* string_prototype = (void*)&string_vtable_instance;
void* array_prototype  = (void*)&array_vtable_instance;
void* vector_prototype = (void*)&vector_vtable;
void* map_prototype    = (void*)&map_vtable;

// Tabela de símbolos globais para modo interativo
#define MAX_GLOBAL_SYMBOLS 1024
static struct {
    char* name;
    void* ptr;
} global_symbols[MAX_GLOBAL_SYMBOLS];
static int global_symbol_count = 0;

void nv_register_global_symbol(const char* name, void* func_ptr) {
    if (global_symbol_count >= MAX_GLOBAL_SYMBOLS) {
        printf("Warning: Global symbol table full\n");
        return;
    }
    
    // Procura por símbolo existente e substitui
    for (int i = 0; i < global_symbol_count; i++) {
        if (strcmp(global_symbols[i].name, name) == 0) {
            global_symbols[i].ptr = func_ptr;
            return;
        }
    }
    
    // Adiciona novo símbolo
    global_symbols[global_symbol_count].name = strdup(name);
    global_symbols[global_symbol_count].ptr = func_ptr;
    global_symbol_count++;
}

void* nv_lookup_global_symbol(const char* name) {
    for (int i = 0; i < global_symbol_count; i++) {
        if (strcmp(global_symbols[i].name, name) == 0) {
            return global_symbols[i].ptr;
        }
    }
    return NULL;
}

// Inicialização do runtime (chamada automaticamente se necessário)
__attribute__((constructor))
static void init_runtime(void) {
    init_type_registry();
}
