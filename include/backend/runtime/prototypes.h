#ifndef PROTOTYPES_H
#define PROTOTYPES_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================= */
/*                    SISTEMA DE TIPOS                          */
/* ============================================================= */

// Tipos base do sistema
#define TAG_INT     1
#define TAG_FLOAT   2
#define TAG_BOOL    3
#define TAG_STR     4
#define TAG_ARRAY   5
#define TAG_VECTOR  6
#define TAG_MAP     7
#define TAG_TUPLE   8
#define TAG_ANY     9
#define TAG_CUSTOM  10  // Placeholder para tipos criados em tempo de compilação (ex: type User = {...})

// Estrutura base Value com rastreio de tipos melhorado
typedef struct {
    int32_t type;           // Tag do tipo (TAG_INT, TAG_FLOAT, etc)
    int64_t value;          // Valor primitivo ou ponteiro
    void* prototype;        // Ponteiro para vtable/metadados do tipo
    void* type_info;        // Metadados adicionais do tipo (para tipos customizados)
    uint32_t flags;         // Flags adicionais (readonly, etc)
} Value;

/* ============================================================= */
/*                    ESTRUTURAS DE DADOS                        */
/* ============================================================= */

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

// Record removido - tipos customizados usam TAG_CUSTOM com TypeInfo

typedef struct {
    Value* fields;
    int field_count;
} Tuple;

/* ============================================================= */
/*                    METADADOS DE TIPOS                        */
/* ============================================================= */

// Estrutura para metadados de tipos customizados criados em tempo de compilação
// Exemplo: type User = { name: string; password: string; }
typedef struct TypeInfo {
    int32_t type_id;                    // ID único do tipo (>= TAG_CUSTOM)
    const char* type_name;               // Nome do tipo (ex: "User")
    size_t size;                         // Tamanho em bytes da estrutura de dados
    void (*destructor)(void*);           // Destrutor do tipo (pode ser NULL)
    void (*printer)(const Value*);       // Função de impressão customizada (pode ser NULL)
    int (*comparator)(const Value*, const Value*); // Comparador (pode ser NULL)
    struct TypeInfo* base_type;          // Tipo base (para herança, pode ser NULL)
    void* custom_data;                   // Dados customizados do tipo (metadados de campos, etc)
    
    // Para tipos struct-like (ex: type User = { name: string; password: string; })
    char** field_names;                  // Nomes dos campos (NULL se não for struct-like)
    int32_t* field_types;                // Tipos dos campos (NULL se não for struct-like)
    int field_count;                     // Número de campos (0 se não for struct-like)
} TypeInfo;

// Registro de tipos dinâmicos
typedef struct TypeRegistry {
    TypeInfo** types;
    int count;
    int capacity;
} TypeRegistry;

/* ============================================================= */
/*                    VTABLES                                   */
/* ============================================================= */

// Ponteiros de função para métodos
typedef void (*MethodPtr1)(Value* out, Value* self);
typedef void (*MethodPtr2)(Value* out, Value* self, Value v);
typedef void (*MethodPtr3)(Value* out, Value* self, Value* a, Value* b);

typedef Value (*MapGetPtr)(Map* m, const char* key);
typedef void  (*MapSetPtr)(Map* m, const char* key, Value val);

typedef void  (*VectorPushPtr)(Vector* v, Value val);
typedef Value (*VectorPopPtr)(Vector* v);
typedef Value (*VectorGetPtr)(Vector* v, int index);
typedef void  (*VectorSetPtr)(Vector* v, int index, Value val);

// VTables para tipos builtin
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
/*                    PROTOTYPES                                */
/* ============================================================= */

// Prototypes globais para tipos builtin
extern void* string_prototype;
extern void* array_prototype;
extern void* vector_prototype;
extern void* map_prototype;

// Registro global de tipos
extern TypeRegistry* global_type_registry;

/* ============================================================= */
/*                    FUNÇÕES DE REGISTRO                       */
/* ============================================================= */

// Inicializar registro de tipos
void init_type_registry(void);

// Registrar um novo tipo customizado
int32_t register_custom_type(TypeInfo* type_info);

// Obter informações de um tipo por ID
TypeInfo* get_type_info(int32_t type_id);

// Obter informações de um tipo por nome
TypeInfo* get_type_info_by_name(const char* name);

// Verificar se um tipo é válido
int is_valid_type(int32_t type);

// Obter nome do tipo como string
const char* get_type_name(int32_t type);

#endif /* PROTOTYPES_H */
