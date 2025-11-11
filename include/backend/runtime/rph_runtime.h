#ifndef RUNTIME_H
#define RUNTIME_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include "prototypes.h"

// tipos de valor
#define TAG_INT     1
#define TAG_FLOAT   2
#define TAG_BOOL    3
#define TAG_STR     4
#define TAG_ARRAY   5
#define TAG_VECTOR  6
#define TAG_MAP     7
#define TAG_TUPLE   8
#define TAG_ANY     9

// api
void create_int(Value* out, int32_t v);
void create_float(Value* out, double v);
void create_bool(Value* out, int b);
void create_str(Value* out, const char* s);
void create_array(Value* out, int size);
void create_vector(Value* out, int capacity);
void create_map(Value* out);
void create_tuple(Value* out, int count);
void create_any(Value* out, Value v);

// métodos
void string_to_upper_case(Value* out, Value* self);
void string_replace(Value* out, Value* self, Value* old_val, Value* new_val);
void string_includes(Value* out, Value* self, Value substr);

void vector_push_method(Value* out, Value* self, const Value* value);
void vector_pop_method(Value* out, Value* self);
void vector_get_method(Value* out, Value* self, int index);
void vector_set_method(Value* self, int index, const Value* value);

void map_get_method(Value* out, Value* self, const char* key);
void map_set_method(Value* self, const char* key, Value value);

Value array_get_index(Array* arr, int index);
void array_set_index(Array* arr, int index, Value value);
void array_get_index_v(Value* out, Value* self, int index);
void array_set_index_v(Value* self, int index, const Value* value);

Value tuple_get_impl(Value* self, int index);
void tuple_set_impl(Value* self, int index, const Value* v);

// impls
void vector_push_impl(Vector* v, Value val);
Value vector_pop_impl(Vector* v);
Value vector_get_impl(Vector* v, int i);
void vector_set_impl(Vector* v, int i, Value val);

Value map_get_impl(Map* m, const char* key);
void map_set_impl(Map* m, const char* key, Value val);

void rph_write(Value* v);
void rph_write_no_nl(Value* v);

// Acesso dinâmico por chave (string)
int any_has(Value* self, const char* key);
void any_get(Value* out, Value* self, const char* key);
void any_set(Value* self, const char* key, Value value);

// Acesso por índice (array/vector)
int any_index_valid(Value* self, int index);
void any_get_index(Value* out, Value* self, int index);
void any_set_index(Value* self, int index, Value value);

#endif /* RUNTIME_H */