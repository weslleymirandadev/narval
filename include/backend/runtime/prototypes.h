#ifndef PROTOTYPES_H
#define PROTOTYPES_H

typedef struct { int32_t type; int64_t value; void* prototype; } Value;

// estruturas
typedef struct { Value* elements; int size; int capacity; } Array;
typedef struct { Value* elements; int size; int capacity; } Vector;
typedef struct { char** keys; Value* values; int size; int capacity; } Map;
typedef struct { char** field_names; Value* fields; int field_count; } Record;
typedef struct { Value* fields; int field_count; } Tuple;

// vtables
typedef void (*MethodPtr1)(Value* out, Value* self);
typedef void (*MethodPtr2)(Value* out, Value* self, Value v);
typedef void (*MethodPtr3)(Value* out, Value* self, Value* a, Value* b);

typedef Value (*MapGetPtr)(Map* m, const char* key);
typedef void  (*MapSetPtr)(Map* m, const char* key, Value val);

typedef void  (*VectorPushPtr)(Vector* v, Value val);
typedef Value (*VectorPopPtr)(Vector* v);
typedef Value (*VectorGetPtr)(Vector* v, int index);
typedef void  (*VectorSetPtr)(Vector* v, int index, Value val);

typedef struct { MethodPtr1 toUpperCase; MethodPtr3 replace; MethodPtr2 includes; } StringVTable;
typedef struct { MethodPtr2 push; MethodPtr1 pop; } ArrayVTable;
typedef struct {
    VectorPushPtr push;
    VectorPopPtr  pop;
    VectorGetPtr  get;
    VectorSetPtr  set;
} VectorVTable;
typedef struct { MapGetPtr get; MapSetPtr set; } MapVTable;

// prototypes
extern void* string_prototype;
extern void* array_prototype;
extern void* vector_prototype;
extern void* map_prototype;

#endif /* PROTOTYPES_H */