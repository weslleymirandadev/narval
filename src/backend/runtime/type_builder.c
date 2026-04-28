#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================= */
/*                    TYPE BUILDER IMPLEMENTATION                */
/* ============================================================= */

struct TypeBuilder {
    char* name;
    NvTypeObject** bases;
    int bases_count;
    int bases_capacity;
    
    NvNumberMethods* number_methods;
    NvSequenceMethods* sequence_methods;
    NvMappingMethods* mapping_methods;
    
    Value** methods;
    int methods_count;
    int methods_capacity;
    
    size_t basicsize;
};

TypeBuilder* type_builder_new(const char* name) {
    if (!name) return NULL;
    
    TypeBuilder* builder = (TypeBuilder*)calloc(1, sizeof(TypeBuilder));
    if (!builder) return NULL;
    
    builder->name = strdup(name);
    builder->bases_capacity = 4;
    builder->bases = (NvTypeObject**)calloc(builder->bases_capacity, sizeof(NvTypeObject*));
    builder->methods_capacity = 8;
    builder->methods = (Value**)calloc(builder->methods_capacity, sizeof(Value*));
    builder->basicsize = sizeof(NvObject);
    
    return builder;
}

void type_builder_add_base(TypeBuilder* builder, NvTypeObject* base) {
    if (!builder || !base) return;
    
    if (builder->bases_count >= builder->bases_capacity) {
        builder->bases_capacity *= 2;
        builder->bases = (NvTypeObject**)realloc(
            builder->bases, 
            builder->bases_capacity * sizeof(NvTypeObject*)
        );
    }
    
    builder->bases[builder->bases_count++] = base;
}

void type_builder_add_method(TypeBuilder* builder, const char* name, Value* method) {
    if (!builder || !name || !method) return;
    
    // Para simplificar, vamos armazenar os métodos como um array de Values
    // Em uma implementação completa, poderíamos usar uma estrutura Method
    
    if (builder->methods_count >= builder->methods_capacity) {
        builder->methods_capacity *= 2;
        builder->methods = (Value**)realloc(
            builder->methods,
            builder->methods_capacity * sizeof(Value*)
        );
    }
    
    builder->methods[builder->methods_count++] = method;
}

void type_builder_set_number_protocol(TypeBuilder* builder, NvNumberMethods* methods) {
    if (!builder) return;
    builder->number_methods = methods;
}

void type_builder_set_sequence_protocol(TypeBuilder* builder, NvSequenceMethods* methods) {
    if (!builder) return;
    builder->sequence_methods = methods;
}

void type_builder_set_mapping_protocol(TypeBuilder* builder, NvMappingMethods* methods) {
    if (!builder) return;
    builder->mapping_methods = methods;
}

void type_builder_set_basicsize(TypeBuilder* builder, size_t size) {
    if (!builder) return;
    builder->basicsize = size;
}

NvTypeObject* type_builder_build(TypeBuilder* builder) {
    if (!builder || !builder->name) return NULL;
    
    // Criar o tipo
    NvTypeObject* type = nv_type_new(builder->name, builder->bases, builder->bases_count);
    if (!type) return NULL;
    
    // Configurar protocolos
    type->tp_as_number = builder->number_methods;
    type->tp_as_sequence = builder->sequence_methods;
    type->tp_as_mapping = builder->mapping_methods;
    
    // Configurar métodos
    if (builder->methods_count > 0) {
        type->tp_methods = (void**)builder->methods;
        type->tp_methods_count = builder->methods_count;
    }
    
    // Configurar tamanho base
    if (builder->basicsize > 0) {
        type->tp_basicsize = builder->basicsize;
    }
    
    // Configurar metaclass
    if (NVType_Type) {
        type->ob_base.ob_type = NVType_Type;
        type->tp_metaclass = NVType_Type;
    }
    
    // Limpar builder (mas não liberar os métodos que agora pertencem ao tipo)
    free(builder->name);
    free(builder->bases);
    free(builder);
    
    return type;
}
