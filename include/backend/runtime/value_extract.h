#ifndef VALUE_EXTRACT_H
#define VALUE_EXTRACT_H

#include "backend/runtime/nv_runtime.h"
#include <stdio.h>
#include <string.h>

int32_t extract_int_from_value(Value* v);
char extract_char_from_value(Value* v);
double extract_float_from_value(Value* v);
int32_t nv_value_cmp(Value* a, Value* b);
char* extract_string_from_value(Value* v);

#endif /* VALUE_EXTRACT_H */
