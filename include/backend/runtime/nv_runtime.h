#ifndef NV_RUNTIME_H
#define NV_RUNTIME_H

#include "backend/runtime/prototypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================= */
/*                    CRIAÇÃO DE VALORES                        */
/* ============================================================= */

// Criar valores básicos
void create_int(Value* out, int32_t value);
void create_float(Value* out, double value);
void create_bool(Value* out, int32_t value);
void create_str(Value* out, const char* value);
void create_array(Value* out);
void create_vector(Value* out);
void create_map(Value* out);
void create_tuple(Value* out);
void create_any(Value* out);

// Inicialização do sistema de tipos
void register_global_init(void);

// Inicializar classes builtin (int, float, str, etc.) como classes completas
void initialize_builtin_classes(void);

/* ============================================================= */
/*                    TRACEBACK EM RUNTIME                       */
/* ============================================================= */

#define NV_TRACE_MAX_FRAMES 64

typedef struct {
    const char* file;
    const char* func;
    int line;
} NvTraceFrame;

extern NvTraceFrame nv_trace_stack[NV_TRACE_MAX_FRAMES];
extern int nv_trace_depth;

void nv_push_frame(const char* file, const char* func);
void nv_pop_frame(void);
void nv_set_line(int line);

// Tratamento de erros em runtime
void nv_raise_value_error(const char* msg);
void nv_raise_type_error(const char* msg);

// Funções de conversão de tipo (estilo Python)
void nv_str_convert(Value* out, Value* input);
void nv_int_convert(Value* out, Value* input);
void nv_float_convert(Value* out, Value* input);
void nv_bool_convert(Value* out, Value* input);

// Funções de conversão para concatenação
void int_to_string(Value* out, int32_t value);
void float_to_string(Value* out, double value);

// Obter tipo de um valor
int32_t get_value_type(const Value* v);

// Obter informações de tipo
TypeInfo* get_value_type_info(const Value* v);

// Garantir tipo do valor
void ensure_value_type(Value* v);

// Validar tipo
int validate_value_type(const Value* v, int32_t expected_type);

// Liberar valor
void free_value(Value* v);

// Aritmética type-aware entre dois Value (int/float detectado em runtime)
void nv_value_add(Value* out, Value* a, Value* b);
void nv_value_sub(Value* out, Value* a, Value* b);
void nv_value_mul(Value* out, Value* a, Value* b);
void nv_value_div(Value* out, Value* a, Value* b);
void nv_value_mod(Value* out, Value* a, Value* b);

// Comparação type-aware: retorna -1, 0 ou 1 (como strcmp)
int32_t nv_value_cmp(Value* a, Value* b);

/* ============================================================= */
/*                    MÉTODOS DE COLEÇÕES                        */
/* ============================================================= */

// Métodos de Map
Value map_get_method(Map* m, const char* key);
void map_set_method(Map* m, const char* key, Value val);

// Métodos de Vector
void vector_push_method(Vector* v, Value val);
Value vector_pop_method(Vector* v);
Value vector_get_method(Vector* v, int index);
void vector_set_method(Vector* v, int index, Value val);

// Métodos de Array
void array_push_method(Array* a, Value val);
Value array_pop_method(Array* a);

/* ============================================================= */
/*                    FUNÇÕES DE I/O                             */
/* ============================================================= */

// Função de escrita principal
void nv_write(Value* v);

/* ============================================================= */
/*                    TIPOS DINÂMICOS DO NARVAL                */
/* ============================================================= */

// Criar novo tipo
NvTypeObject* nv_type_create(const char* name, NvTypeObject** bases, int bases_count, Value** attributes, int attr_count);

// Criar classe simples
NvTypeObject* nv_create_simple_class(const char* name);

// Criar classe com herança
NvTypeObject* nv_create_class_with_base(const char* name, NvTypeObject* base);

// Criar classe com múltiplas heranças
NvTypeObject* nv_create_class_with_bases(const char* name, ...);

// Type Builder API
typedef struct TypeBuilder TypeBuilder;
TypeBuilder* type_builder_new(const char* name);
void type_builder_add_base(TypeBuilder* builder, NvTypeObject* base);
void type_builder_add_method(TypeBuilder* builder, const char* name, Value* method);
void type_builder_set_number_protocol(TypeBuilder* builder, NvNumberMethods* methods);
void type_builder_set_sequence_protocol(TypeBuilder* builder, NvSequenceMethods* methods);
void type_builder_set_mapping_protocol(TypeBuilder* builder, NvMappingMethods* methods);
NvTypeObject* type_builder_build(TypeBuilder* builder);

// Acesso a campos de objetos (instâncias de classes)
// Convenção: void fn(Value* out, Value* self, const char* key)  /  void fn(Value* self, const char* key, Value* val)
void nv_object_get_field(Value* out, Value* self, const char* key);
void nv_object_set_field(Value* self, const char* key, Value* val);

// Funções de debug
void nv_type_print_info(NvTypeObject* type);
void nv_object_print_type(NvObject* obj);
void nv_test_isinstance(NvObject* obj, NvTypeObject* type);

/* ============================================================= */
/*                    LEGADO - COMPATIBILIDADE                   */
/* ============================================================= */

// Criar novo tipo customizado em runtime (legado)
int32_t create_dynamic_type(const char* type_name, int field_count, char** field_names, int32_t* field_types);

// Criar tipo struct-like simples (com número variável de argumentos)
int32_t create_struct_type(const char* type_name, ...);

// Criar instância de tipo customizado
void create_dynamic_instance(Value* out, int32_t type_id, Value* field_values);

// Acessar campo de tipo customizado
void get_custom_field(Value* out, Value* instance, const char* field_name);

// Definir campo de tipo customizado
void set_custom_field(Value* instance, const char* field_name, Value value);

// Converter para tipo dinâmico
int convert_to_dynamic(Value* out, const Value* in, int32_t target_type);

#ifdef __cplusplus
}
#endif

#endif /* NV_RUNTIME_H */
