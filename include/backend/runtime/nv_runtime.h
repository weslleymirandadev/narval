#ifndef RUNTIME_H
#define RUNTIME_H

#include "prototypes.h"
#include <stdint.h>

/* ============================================================= */
/*                    CRIAÇÃO DE VALORES                        */
/* ============================================================= */

// Criar valores primitivos (garantem tag correta)
void create_int(Value* out, int32_t v);
void create_float(Value* out, double v);
void create_bool(Value* out, int b);
void create_str(Value* out, const char* s);

// Criar estruturas de dados
void create_array(Value* out, int size);
void create_vector(Value* out, int capacity);
void create_map(Value* out);
void create_tuple(Value* out, int count);
void create_any(Value* out, Value v);

// Criar tipo customizado (placeholder para tipos criados em tempo de compilação)
// Exemplo: type User = { name: string; password: string; }
// O type_id deve ser >= TAG_CUSTOM e estar registrado no TypeRegistry
void create_custom(Value* out, int32_t type_id, void* data);

// Criar tipo customizado struct-like (helper para tipos como User acima)
// Aloca memória e inicializa campos baseado no TypeInfo
void create_custom_struct(Value* out, int32_t type_id, Value* field_values);

/* ============================================================= */
/*                    MÉTODOS DE STRING                         */
/* ============================================================= */

void string_to_upper_case(Value* out, Value* self);
void string_replace(Value* out, Value* self, Value* old_val, Value* new_val);
void string_includes(Value* out, Value* self, Value substr);

/* ============================================================= */
/*                    MÉTODOS DE VECTOR                         */
/* ============================================================= */

void vector_push_method(Value* out, Value* self, const Value* value);
void vector_pop_method(Value* out, Value* self);
void vector_get_method(Value* out, Value* self, int index);
void vector_set_method(Value* self, int index, const Value* value);

/* ============================================================= */
/*                    MÉTODOS DE MAP                             */
/* ============================================================= */

void map_get_method(Value* out, Value* self, const char* key);
void map_set_method(Value* self, const char* key, Value value);

/* ============================================================= */
/*                    OPERAÇÕES DE ARRAY                        */
/* ============================================================= */

Value array_get_index(Array* arr, int index);
void array_set_index(Array* arr, int index, Value value);
void array_get_index_v(Value* out, Value* self, int index);
void array_set_index_v(Value* self, int index, const Value* value);

/* ============================================================= */
/*                    OPERAÇÕES DE TUPLE                        */
/* ============================================================= */

Value tuple_get_impl(Value* self, int index);
void tuple_set_impl(Value* self, int index, const Value* v);

/* ============================================================= */
/*                    IMPLEMENTAÇÕES INTERNAS                    */
/* ============================================================= */

void vector_push_impl(Vector* v, Value val);
Value vector_pop_impl(Vector* v);
Value vector_get_impl(Vector* v, int i);
void vector_set_impl(Vector* v, int i, Value val);

Value map_get_impl(Map* m, const char* key);
void map_set_impl(Map* m, const char* key, Value val);

/* ============================================================= */
/*                    I/O E PRINT                               */
/* ============================================================= */

void nv_write(Value* v);
void nv_write_no_nl(Value* v);

/* ============================================================= */
/*                    OPERAÇÕES JSON                             */
/* ============================================================= */

void json_load(Value* out, const char* filename);

/* ============================================================= */
/*                    ACESSO DINÂMICO                           */
/* ============================================================= */

// Acesso por chave (string)
int any_has(Value* self, const char* key);
void any_get(Value* out, Value* self, const char* key);
void any_set(Value* self, const char* key, Value value);

// Acesso por índice (array/vector)
int any_index_valid(Value* self, int index);
void any_get_index(Value* out, Value* self, int index);
void any_set_index(Value* self, int index, Value value);

/* ============================================================= */
/*                    RASTREIO E VALIDAÇÃO DE TIPOS             */
/* ============================================================= */

// Garantir que um Value tenha a tag correta (com rastreio melhorado)
void ensure_value_type(Value* v);

// Validar tipo de um Value
int validate_value_type(const Value* v);

// Obter tipo de um Value (com validação)
int32_t get_value_type(const Value* v);

// Verificar compatibilidade de tipos
int types_compatible(int32_t type1, int32_t type2);

// Converter tipo (se possível)
int convert_type(Value* out, const Value* in, int32_t target_type);

// Obter informações de tipo de um Value
TypeInfo* get_value_type_info(const Value* v);

// Imprimir informações de tipo (para debug)
void print_type_info(const Value* v);

/* ============================================================= */
/*                    GARBAGE COLLECTION                         */
/* ============================================================= */

// Marcar valor para GC (se necessário)
void gc_mark_value(const Value* v);

// Liberar valor (chama destructor se necessário)
void free_value(Value* v);

#endif /* RUNTIME_H */
