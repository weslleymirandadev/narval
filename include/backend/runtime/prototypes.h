#ifndef PROTOTYPES_H
#define PROTOTYPES_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================= */
/*                    SISTEMA DE TIPOS DO NARVAL                 */
/* ============================================================= */

// Estrutura base NvObject - tudo em Narval é um objeto
typedef struct NvObject {
    struct NvTypeObject* ob_type;    // Ponteiro para o tipo do objeto
    int32_t ref_count;               // Contador de referências (GC)
    uint32_t flags;                  // Flags adicionais
} NvObject;

// Estrutura NvTypeObject - representação de tipos
typedef struct NvTypeObject {
    NvObject ob_base;                   // NvObject base (o tipo também é um objeto!)
    
    const char* tp_name;            // Nome do tipo (ex: "int", "str", "MeuTipo")
    size_t tp_basicsize;            // Tamanho base do objeto
    
    // Construtores e destrutores
    void* (*tp_new)(struct NvTypeObject*, void* args, void* kw);
    void (*tp_dealloc)(struct NvObject*);
    
    // Métodos especiais (__add__, __len__, etc)
    void* tp_as_number;             // Protocolo numérico
    void* tp_as_sequence;           // Protocolo de sequência
    void* tp_as_mapping;            // Protocolo de mapping
    
    // Métodos do objeto
    void** tp_methods;               // Array de métodos
    int tp_methods_count;
    
    // Herança
    struct NvTypeObject** tp_bases;  // Tipos base
    int tp_bases_count;
    
    // Metaclasses
    struct NvTypeObject* tp_metaclass;  // A metaclass (geralmente 'type')
    
    // Cache para performance
    void* tp_cache;
    
} NvTypeObject;

// Macro para header de NvObject
#define NvObject_HEAD NvObject ob_base;

// Tipos primitivos base (todos derivam destes)
#define NV_INT_BASE    1
#define NV_FLOAT_BASE  2
#define NV_BOOL_BASE   3
#define NV_STR_BASE    4
#define NV_ARRAY_BASE  5
#define NV_VECTOR_BASE 6
#define NV_MAP_BASE    7
#define NV_TUPLE_BASE  8

// Tipos especiais
#define NV_TYPE_BASE   9   // O tipo 'type' mesmo
#define NV_OBJECT_BASE 10  // O tipo 'object' base
#define NV_ANY_BASE    11  // Tipo dinâmico (como 'any' em linguagens modernas)

// Tipos de exceção
#define NV_ERROR_BASE      12  // Base Error class
#define NV_VALUE_ERROR     13  // ValueError
#define NV_TYPE_ERROR      14  // TypeError
#define NV_RUNTIME_ERROR   15  // RuntimeError
#define NV_INDEX_ERROR     16  // IndexError
#define NV_KEY_ERROR       17  // KeyError
#define NV_ATTRIBUTE_ERROR 18  // AttributeError
#define NV_NAME_ERROR      19  // NameError
#define NV_ASSERTION_ERROR 20  // AssertionError

// Tipos Option / Result
#define NV_OPTION_NONE_BASE 21
#define NV_OPTION_SOME_BASE 22
#define NV_RESULT_OK_BASE   23
#define NV_RESULT_ERR_BASE  24

// Estrutura Value simplificada - agora é apenas um wrapper para NvObject
typedef struct {
    NvObject* obj;                  // Ponteiro para o objeto real
} Value;

/* ============================================================= */
/*                    ESTRUTURAS DE DADOS                        */
/* ============================================================= */

// Tipos primitivos herdam de NvObject
typedef struct {
    NvObject_HEAD;
    int32_t value;
} NVInt;

typedef struct {
    NvObject_HEAD;
    double value;
} NVFloat;

typedef struct {
    NvObject_HEAD;
    int32_t value;
} NVBool;

typedef struct {
    NvObject_HEAD;
    char* value;
    size_t len;
} NVStr;

typedef struct {
    NvObject_HEAD;
    Value* elements;
    int size;
    int capacity;
} NVArray;

typedef struct {
    NvObject_HEAD;
    Value* elements;
    int size;
    int capacity;
} NVVector;

typedef struct {
    NvObject_HEAD;
    char** keys;
    Value* values;
    int size;
    int capacity;
    Value (*str_method)(Value* self);  // __str__ dunder, NULL if not defined
} NVMap;

typedef struct {
    NvObject_HEAD;
    Value* fields;
    int field_count;
} NVTuple;

// Tipos customizados (classes definidas pelo usuário)
typedef struct {
    NvObject_HEAD;
    Value* attributes;             // Dicionário de atributos
    Value* __dict__;               // Namespace do objeto
} NVObject;

// O tipo 'type' (metaclass base)
typedef struct {
    NvTypeObject base;
    // Métodos da metaclass
} NVType;

/* ============================================================= */
/*                    ESTRUTURAS DE EXCEÇÃO                      */
/* ============================================================= */

// Base Error class - todas as exceções herdam desta
typedef struct {
    NvObject_HEAD;
    char* message;      // Mensagem de erro
    char* traceback;    // Stack trace (opcional)
} NVError;

// ValueError - erro em valor de argumento
typedef struct {
    NvObject_HEAD;
    char* message;
    char* traceback;
} NVValueError;

// TypeError - erro de tipo
typedef struct {
    NvObject_HEAD;
    char* message;
    char* traceback;
} NVTypeError;

// RuntimeError - erro em runtime
typedef struct {
    NvObject_HEAD;
    char* message;
    char* traceback;
} NVRuntimeError;

// IndexError - erro de índice
typedef struct {
    NvObject_HEAD;
    char* message;
    char* traceback;
} NVIndexError;

// KeyError - erro de chave
typedef struct {
    NvObject_HEAD;
    char* message;
    char* traceback;
} NVKeyError;

// AttributeError - erro de atributo
typedef struct {
    NvObject_HEAD;
    char* message;
    char* traceback;
} NVAttributeError;

// NameError - erro de nome
typedef struct {
    NvObject_HEAD;
    char* message;
    char* traceback;
} NVNameError;

// AssertionError - erro de asserção
typedef struct {
    NvObject_HEAD;
    char* message;
    char* traceback;
} NVAssertionError;

/* ============================================================= */
/*                    OPTION / RESULT                            */
/* ============================================================= */

typedef struct { NvObject_HEAD; }        NVOptionNone;
typedef struct { NvObject_HEAD; Value inner; } NVOptionSome;
typedef struct { NvObject_HEAD; Value inner; } NVResultOk;
typedef struct { NvObject_HEAD; Value inner; } NVResultErr;

extern NvTypeObject* NVOptionNone_Type;
extern NvTypeObject* NVOptionSome_Type;
extern NvTypeObject* NVResultOk_Type;
extern NvTypeObject* NVResultErr_Type;

// Estrutura para definição de tipos dinâmicos (equivalente a criar classes)
typedef struct TypeSpec {
    const char* name;
    NvTypeObject** bases;
    int bases_count;
    Value** attributes;
    int attributes_count;
} TypeSpec;

/* ============================================================= */
/*                    PROTOCOLOS E INTERFACES                    */
/* ============================================================= */

// Protocolo numérico
typedef struct {
    Value* (*nb_add)(Value* a, Value* b);
    Value* (*nb_subtract)(Value* a, Value* b);
    Value* (*nb_multiply)(Value* a, Value* b);
    Value* (*nb_divide)(Value* a, Value* b);
    // ... outros operadores
} NvNumberMethods;

// Protocolo de sequência
typedef struct {
    int (*sq_length)(Value* obj);
    Value* (*sq_getitem)(Value* obj, int index);
    void (*sq_setitem)(Value* obj, int index, Value* value);
    // ... outros métodos de sequência
} NvSequenceMethods;

// Protocolo de mapping
typedef struct {
    int (*mp_length)(Value* obj);
    Value* (*mp_getitem)(Value* obj, Value* key);
    void (*mp_setitem)(Value* obj, Value* key, Value* value);
    // ... outros métodos de mapping
} NvMappingMethods;

/* ============================================================= */
/*                    REGISTRO DE TIPOS                          */
/* ============================================================= */

// Registro global de tipos
typedef struct TypeRegistry {
    NvTypeObject** types;
    int count;
    int capacity;
} TypeRegistry;

/* ============================================================= */
/*                    VTABLES (LEGADO - COMPATIBILIDADE)        */
/* ============================================================= */

// Estruturas legadas para compatibilidade
typedef struct {
    Value* elements;
    int size;
    int capacity;
} Array;

typedef struct {
    Value* elements;
    int size;
    int capacity;
} Vector;

typedef struct {
    char** keys;
    Value* values;
    int size;
    int capacity;
} Map;

// Metadados de tipos legados
typedef struct TypeInfo {
    int32_t type_id;
    const char* type_name;
    size_t size;
    void (*destructor)(void*);
    void (*printer)(const Value*);
    int (*comparator)(const Value*, const Value*);
    struct TypeInfo* base_type;
    void* custom_data;
    char** field_names;
    int32_t* field_types;
    int field_count;
} TypeInfo;

// Mantendo por compatibilidade temporária
typedef void (*MethodPtr1)(Value* out, Value* self);
typedef void (*MethodPtr2)(Value* out, Value* self, Value v);
typedef void (*MethodPtr3)(Value* out, Value* self, Value a, Value b);

typedef Value (*MapGetPtr)(Map* m, const char* key);
typedef void  (*MapSetPtr)(Map* m, const char* key, Value val);

typedef void  (*VectorPushPtr)(Vector* v, Value val);
typedef Value (*VectorPopPtr)(Vector* v);
typedef Value (*VectorGetPtr)(Vector* v, int index);
typedef void  (*VectorSetPtr)(Vector* v, int index, Value val);

// VTables para tipos builtin (legado)
typedef struct {
    MethodPtr1 toUpperCase;
    MethodPtr3 replace;
    MethodPtr2 includes;
} StringVTable;

typedef struct {
    MethodPtr2 push;
    MethodPtr1 pop;
} ArrayVTable;

typedef struct {
    VectorPushPtr push;
    VectorPopPtr  pop;
    VectorGetPtr  get;
    VectorSetPtr  set;
} VectorVTable;

typedef struct {
    MapGetPtr get;
    MapSetPtr set;
} MapVTable;

/* ============================================================= */
/*                    PROTÓTIPOS GLOBAIS                         */
/* ============================================================= */

// Tipos builtin globais
extern void* string_prototype;
extern void* array_prototype;
extern void* vector_prototype;
extern void* map_prototype;

// Registro global de tipos
extern TypeRegistry* global_type_registry;

// Tipos builtin globais
extern NvTypeObject* NVInt_Type;
extern NvTypeObject* NVFloat_Type;
extern NvTypeObject* NVBool_Type;
extern NvTypeObject* NVStr_Type;
extern NvTypeObject* NVArray_Type;
extern NvTypeObject* NVVector_Type;
extern NvTypeObject* NVMap_Type;
extern NvTypeObject* NVTuple_Type;
extern NvTypeObject* NVObject_Type;
extern NvTypeObject* NVType_Type;  // O tipo 'type'

/* ============================================================= */
/*                    FUNÇÕES DO SISTEMA DE TIPOS                */
/* ============================================================= */

// Inicializar sistema de tipos
void nv_type_system_init(void);

// Criar novo tipo
NvTypeObject* nv_type_new(const char* name, NvTypeObject** bases, int bases_count);

// Criar instância de um tipo
NvObject* nv_object_new(NvTypeObject* type, void* args, void* kwargs);

// Obter tipo de um objeto
NvTypeObject* nv_object_type(NvObject* obj);

// Verificar se objeto é instância de tipo
int nv_object_isinstance(NvObject* obj, NvTypeObject* type);

// Verificar se subclasse
int nv_type_issubclass(NvTypeObject* subclass, NvTypeObject* superclass);

// Duck typing - verificar se objeto tem método/atributo
int nv_object_hasattr(NvObject* obj, const char* name);
Value* nv_object_getattr(NvObject* obj, const char* name);
void nv_object_setattr(NvObject* obj, const char* name, Value* value);

// Protocolos
int nv_object_supports_protocol(NvObject* obj, const char* protocol_name);

/* ============================================================= */
/*                    FUNÇÕES DE REGISTRO (LEGADO)              */
/* ============================================================= */

// Inicializar registro de tipos (legado)
void init_type_registry(void);

// Registrar um novo tipo customizado (legado)
int32_t register_custom_type(TypeInfo* type_info);

// Obter informações de um tipo por ID (legado)
TypeInfo* get_type_info(int32_t type_id);

// Obter informações de um tipo por nome (legado)
TypeInfo* get_type_info_by_name(const char* name);

// Verificar se um tipo é válido (legado)
int is_valid_type(int32_t type);

// Obter nome do tipo como string (legado)
const char* get_type_name(int32_t type);

// Liberar TypeInfo (legado)
void free_type_info(TypeInfo* info);

/* ============================================================= */
/*                    FUNÇÕES DE CRIAÇÃO DE VALORES            */
/* ============================================================= */

// Obter tipo de um valor
int32_t get_value_type(const Value* v);

// Obter informações de tipo (legado)
TypeInfo* get_value_type_info_legacy(const Value* v);

// Imprimir informações de tipo (para debug)
void print_type_info(const Value* v);

// Converter tipo (se possível)
int convert_type(Value* out, const Value* in, int32_t target_type);

// Validar valor (legado)
int validate_value(const Value* v);

// Copiar valor (legado)
void copy_value(Value* dest, const Value* src);

// Comparar valores (legado)
int compare_values(const Value* a, const Value* b);

// Verificar se valor é nulo (legado)
int is_null_value(const Value* v);

// Criar valor nulo (legado)
void create_null(Value* out);

// Criar valores básicos
void create_int(Value* out, int32_t value);
void create_float(Value* out, double value);
void create_bool(Value* out, int32_t value);
void create_str(Value* out, const char* value);

/* ============================================================= */
/*                    FUNÇÕES DE GERENCIAMENTO DE MEMÓRIA       */
/* ============================================================= */

// Incrementar contador de referências
static inline void nv_incref(NvObject* obj) {
    if (obj) {
        obj->ref_count++;
    }
}

// Decrementar contador de referências
static inline void nv_decref(NvObject* obj) {
    if (obj && --obj->ref_count == 0) {
        // Liberar objeto
        if (obj->ob_type && obj->ob_type->tp_dealloc) {
            obj->ob_type->tp_dealloc(obj);
        } else {
            free(obj);
        }
    }
}

#endif /* PROTOTYPES_H */
