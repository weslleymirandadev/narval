#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================= */
/*                    MÉTODOS BUILTIN NUMÉRICOS                  */
/* ============================================================= */

static Value* builtin_add(Value* a, Value* b) {
    if (!a || !b || !a->obj || !b->obj) return NULL;
    
    // Verificar se ambos são numéricos
    NvTypeObject* type_a = a->obj->ob_type;
    NvTypeObject* type_b = b->obj->ob_type;
    
    if (!type_a || !type_b) return NULL;
    
    // Criar resultado
    Value* result = (Value*)calloc(1, sizeof(Value));
    if (!result) return NULL;
    
    // Int + Int = Int
    if (type_a == NVInt_Type && type_b == NVInt_Type) {
        NVInt* int_a = (NVInt*)a->obj;
        NVInt* int_b = (NVInt*)b->obj;
        create_int(result, int_a->value + int_b->value);
        return result;
    }
    
    // Float + Float = Float
    if (type_a == NVFloat_Type && type_b == NVFloat_Type) {
        NVFloat* float_a = (NVFloat*)a->obj;
        NVFloat* float_b = (NVFloat*)b->obj;
        create_float(result, float_a->value + float_b->value);
        return result;
    }
    
    // Int + Float = Float (promoção)
    if ((type_a == NVInt_Type && type_b == NVFloat_Type) ||
        (type_a == NVFloat_Type && type_b == NVInt_Type)) {
        double val_a = (type_a == NVFloat_Type) ? 
                       ((NVFloat*)a->obj)->value : 
                       ((NVInt*)a->obj)->value;
        double val_b = (type_b == NVFloat_Type) ? 
                       ((NVFloat*)b->obj)->value : 
                       ((NVInt*)b->obj)->value;
        create_float(result, val_a + val_b);
        return result;
    }
    
    free(result);
    return NULL;
}

static Value* builtin_subtract(Value* a, Value* b) {
    if (!a || !b || !a->obj || !b->obj) return NULL;
    
    NvTypeObject* type_a = a->obj->ob_type;
    NvTypeObject* type_b = b->obj->ob_type;
    
    if (!type_a || !type_b) return NULL;
    
    Value* result = (Value*)calloc(1, sizeof(Value));
    if (!result) return NULL;
    
    // Int - Int = Int
    if (type_a == NVInt_Type && type_b == NVInt_Type) {
        NVInt* int_a = (NVInt*)a->obj;
        NVInt* int_b = (NVInt*)b->obj;
        create_int(result, int_a->value - int_b->value);
        return result;
    }
    
    // Float - Float = Float
    if (type_a == NVFloat_Type && type_b == NVFloat_Type) {
        NVFloat* float_a = (NVFloat*)a->obj;
        NVFloat* float_b = (NVFloat*)b->obj;
        create_float(result, float_a->value - float_b->value);
        return result;
    }
    
    // Int - Float = Float (promoção)
    if ((type_a == NVInt_Type && type_b == NVFloat_Type) ||
        (type_a == NVFloat_Type && type_b == NVInt_Type)) {
        double val_a = (type_a == NVFloat_Type) ? 
                       ((NVFloat*)a->obj)->value : 
                       ((NVInt*)a->obj)->value;
        double val_b = (type_b == NVFloat_Type) ? 
                       ((NVFloat*)b->obj)->value : 
                       ((NVInt*)b->obj)->value;
        create_float(result, val_a - val_b);
        return result;
    }
    
    free(result);
    return NULL;
}

static Value* builtin_multiply(Value* a, Value* b) {
    if (!a || !b || !a->obj || !b->obj) return NULL;
    
    NvTypeObject* type_a = a->obj->ob_type;
    NvTypeObject* type_b = b->obj->ob_type;
    
    if (!type_a || !type_b) return NULL;
    
    Value* result = (Value*)calloc(1, sizeof(Value));
    if (!result) return NULL;
    
    // Int * Int = Int
    if (type_a == NVInt_Type && type_b == NVInt_Type) {
        NVInt* int_a = (NVInt*)a->obj;
        NVInt* int_b = (NVInt*)b->obj;
        create_int(result, int_a->value * int_b->value);
        return result;
    }
    
    // Float * Float = Float
    if (type_a == NVFloat_Type && type_b == NVFloat_Type) {
        NVFloat* float_a = (NVFloat*)a->obj;
        NVFloat* float_b = (NVFloat*)b->obj;
        create_float(result, float_a->value * float_b->value);
        return result;
    }
    
    // Int * Float = Float (promoção)
    if ((type_a == NVInt_Type && type_b == NVFloat_Type) ||
        (type_a == NVFloat_Type && type_b == NVInt_Type)) {
        double val_a = (type_a == NVFloat_Type) ? 
                       ((NVFloat*)a->obj)->value : 
                       ((NVInt*)a->obj)->value;
        double val_b = (type_b == NVFloat_Type) ? 
                       ((NVFloat*)b->obj)->value : 
                       ((NVInt*)b->obj)->value;
        create_float(result, val_a * val_b);
        return result;
    }
    
    free(result);
    return NULL;
}

static Value* builtin_divide(Value* a, Value* b) {
    if (!a || !b || !a->obj || !b->obj) return NULL;
    
    NvTypeObject* type_a = a->obj->ob_type;
    NvTypeObject* type_b = b->obj->ob_type;
    
    if (!type_a || !type_b) return NULL;
    
    Value* result = (Value*)calloc(1, sizeof(Value));
    if (!result) return NULL;
    
    // Verificar divisão por zero
    if (type_b == NVInt_Type) {
        NVInt* int_b = (NVInt*)b->obj;
        if (int_b->value == 0) {
            free(result);
            return NULL; // Divisão por zero
        }
    } else if (type_b == NVFloat_Type) {
        NVFloat* float_b = (NVFloat*)b->obj;
        if (float_b->value == 0.0) {
            free(result);
            return NULL; // Divisão por zero
        }
    }
    
    // Int / Int = Float (divisão sempre retorna float)
    if (type_a == NVInt_Type && type_b == NVInt_Type) {
        NVInt* int_a = (NVInt*)a->obj;
        NVInt* int_b = (NVInt*)b->obj;
        create_float(result, (double)int_a->value / (double)int_b->value);
        return result;
    }
    
    // Float / Float = Float
    if (type_a == NVFloat_Type && type_b == NVFloat_Type) {
        NVFloat* float_a = (NVFloat*)a->obj;
        NVFloat* float_b = (NVFloat*)b->obj;
        create_float(result, float_a->value / float_b->value);
        return result;
    }
    
    // Int / Float = Float (promoção)
    if ((type_a == NVInt_Type && type_b == NVFloat_Type) ||
        (type_a == NVFloat_Type && type_b == NVInt_Type)) {
        double val_a = (type_a == NVFloat_Type) ? 
                       ((NVFloat*)a->obj)->value : 
                       ((NVInt*)a->obj)->value;
        double val_b = (type_b == NVFloat_Type) ? 
                       ((NVFloat*)b->obj)->value : 
                       ((NVInt*)b->obj)->value;
        if (val_b != 0.0) {
            create_float(result, val_a / val_b);
            return result;
        }
    }
    
    free(result);
    return NULL;
}

/* ============================================================= */
/*                    ARITMÉTICA PÚBLICA TYPE-AWARE               */
/* ============================================================= */

static void copy_and_free(Value* out, Value* result) {
    if (result) { *out = *result; free(result); } else { out->obj = NULL; }
}

void nv_value_add(Value* out, Value* a, Value* b) {
    if (!out) return;
    copy_and_free(out, builtin_add(a, b));
}

void nv_value_sub(Value* out, Value* a, Value* b) {
    if (!out) return;
    copy_and_free(out, builtin_subtract(a, b));
}

void nv_value_mul(Value* out, Value* a, Value* b) {
    if (!out) return;
    copy_and_free(out, builtin_multiply(a, b));
}

void nv_value_div(Value* out, Value* a, Value* b) {
    if (!out) return;
    copy_and_free(out, builtin_divide(a, b));
}

void nv_value_mod(Value* out, Value* a, Value* b) {
    if (!out || !a || !b || !a->obj || !b->obj) { if (out) out->obj = NULL; return; }
    NvTypeObject* ta = a->obj->ob_type;
    NvTypeObject* tb = b->obj->ob_type;
    if (ta == NVInt_Type && tb == NVInt_Type) {
        int32_t va = ((NVInt*)a->obj)->value;
        int32_t vb = ((NVInt*)b->obj)->value;
        if (vb == 0) { out->obj = NULL; return; }
        create_int(out, va % vb);
    } else {
        double va = (ta == NVFloat_Type) ? ((NVFloat*)a->obj)->value : (double)((NVInt*)a->obj)->value;
        double vb = (tb == NVFloat_Type) ? ((NVFloat*)b->obj)->value : (double)((NVInt*)b->obj)->value;
        if (vb == 0.0) { out->obj = NULL; return; }
        extern double fmod(double, double);
        create_float(out, fmod(va, vb));
    }
}

/* ============================================================= */
/*                    MÉTODOS BUILTIN DE STRING                   */
/* ============================================================= */

static int builtin_str_length(Value* obj) {
    if (!obj || !obj->obj) return 0;
    
    NvTypeObject* type = obj->obj->ob_type;
    if (type == NVStr_Type) {
        NVStr* str_obj = (NVStr*)obj->obj;
        return str_obj->len;
    }
    
    return 0;
}

static Value* builtin_str_upper(Value* obj) {
    if (!obj || !obj->obj) return NULL;
    
    NvTypeObject* type = obj->obj->ob_type;
    if (type != NVStr_Type) return NULL;
    
    NVStr* str_obj = (NVStr*)obj->obj;
    if (!str_obj->value) return NULL;
    
    Value* result = (Value*)calloc(1, sizeof(Value));
    if (!result) return NULL;
    
    // Alocar espaço para string maiúscula
    size_t len = strlen(str_obj->value);
    char* upper_str = (char*)malloc(len + 1);
    if (!upper_str) {
        free(result);
        return NULL;
    }
    
    // Converter para maiúscula
    for (size_t i = 0; i < len; i++) {
        upper_str[i] = toupper(str_obj->value[i]);
    }
    upper_str[len] = '\0';
    
    create_str(result, upper_str);
    free(upper_str);
    return result;
}

/* ============================================================= */
/*                    FUNÇÕES HELPER DE CRIAÇÃO                  */
/* ============================================================= */

static NvTypeObject* create_builtin_numeric_class(const char* name, int base_id) {
    TypeBuilder* builder = type_builder_new(name);
    if (!builder) return NULL;
    
    // Adicionar protocolo numérico
    NvNumberMethods* num_methods = (NvNumberMethods*)calloc(1, sizeof(NvNumberMethods));
    if (!num_methods) return NULL;
    
    num_methods->nb_add = builtin_add;
    num_methods->nb_subtract = builtin_subtract;
    num_methods->nb_multiply = builtin_multiply;
    num_methods->nb_divide = builtin_divide;
    
    type_builder_set_number_protocol(builder, num_methods);
    
    // Definir tamanho base correto para cada tipo
    NvTypeObject* type = type_builder_build(builder);
    if (type) {
        if (strcmp(name, "int") == 0) {
            type->tp_basicsize = sizeof(NVInt);
        } else if (strcmp(name, "float") == 0) {
            type->tp_basicsize = sizeof(NVFloat);
        } else if (strcmp(name, "bool") == 0) {
            type->tp_basicsize = sizeof(NVBool);
        } else if (strcmp(name, "char") == 0) {
            type->tp_basicsize = sizeof(NVChar);
        }
    }
    
    return type;
}

static NvTypeObject* create_builtin_sequence_class(const char* name, int base_id) {
    TypeBuilder* builder = type_builder_new(name);
    if (!builder) return NULL;
    
    // Adicionar protocolo de sequência para strings
    if (strcmp(name, "str") == 0) {
        NvSequenceMethods* seq_methods = (NvSequenceMethods*)calloc(1, sizeof(NvSequenceMethods));
        if (seq_methods) {
            seq_methods->sq_length = builtin_str_length;
            type_builder_set_sequence_protocol(builder, seq_methods);
        }
        
        // Definir tamanho base para string
        NvTypeObject* type = type_builder_build(builder);
        if (type) {
            type->tp_basicsize = sizeof(NVStr);
        }
        return type;
    }
    
    // Para outros tipos de sequência (array, vector, tuple)
    NvTypeObject* type = type_builder_build(builder);
    if (type) {
        if (strcmp(name, "array") == 0) {
            type->tp_basicsize = sizeof(NVArray);
        } else if (strcmp(name, "vector") == 0) {
            type->tp_basicsize = sizeof(NVVector);
        } else if (strcmp(name, "tuple") == 0) {
            type->tp_basicsize = sizeof(NVTuple);
        }
    }
    
    return type;
}

static NvTypeObject* create_builtin_mapping_class(const char* name, int base_id) {
    TypeBuilder* builder = type_builder_new(name);
    if (!builder) return NULL;
    
    NvTypeObject* type = type_builder_build(builder);
    if (type) {
        if (strcmp(name, "map") == 0) {
            type->tp_basicsize = sizeof(NVMap);
        }
    }
    
    return type;
}

/* ============================================================= */
/*                    FUNÇÃO PRINCIPAL DE INICIALIZAÇÃO            */
/* ============================================================= */

void initialize_builtin_classes(void) {
    // Criar tipos builtin como classes completas
    NVInt_Type = create_builtin_numeric_class("int", NV_INT_BASE);
    NVFloat_Type = create_builtin_numeric_class("float", NV_FLOAT_BASE);
    NVBool_Type = create_builtin_numeric_class("bool", NV_BOOL_BASE);
    NVChar_Type = create_builtin_numeric_class("char", NV_CHAR_BASE);
    
    NVStr_Type = create_builtin_sequence_class("str", NV_STR_BASE);
    NVArray_Type = create_builtin_sequence_class("array", NV_ARRAY_BASE);
    NVVector_Type = create_builtin_sequence_class("vector", NV_VECTOR_BASE);
    NVTuple_Type = create_builtin_sequence_class("tuple", NV_TUPLE_BASE);
    
    NVMap_Type = create_builtin_mapping_class("map", NV_MAP_BASE);
    
    // Object e Type já são criados normalmente
    NVObject_Type = nv_type_new("object", NULL, 0);
    NVType_Type = nv_type_new("type", NULL, 0);
    
    // Configurar metaclasses
    if (NVType_Type) {
        NVType_Type->ob_base.ob_type = NVType_Type;  // type é sua própria metaclass
        NVType_Type->tp_metaclass = NVType_Type;
    }
    
    // Object herda de type (como em Python)
    if (NVObject_Type && NVType_Type) {
        NVObject_Type->ob_base.ob_type = NVType_Type;
        NVObject_Type->tp_metaclass = NVType_Type;
    }
    
    // Criar tipos de exceção
    initialize_exception_types();

    // Criar tipos Option/Result
    initialize_option_result_types();
}

/* ============================================================= */
/*                    INICIALIZAÇÃO DE EXCEÇÕES                  */
/* ============================================================= */

// Definições globais dos tipos de exceção
NvTypeObject* NVError_Type = NULL;
NvTypeObject* NVValueError_Type = NULL;
NvTypeObject* NVTypeError_Type = NULL;
NvTypeObject* NVRuntimeError_Type = NULL;
NvTypeObject* NVIndexError_Type = NULL;
NvTypeObject* NVKeyError_Type = NULL;
NvTypeObject* NVAttributeError_Type = NULL;
NvTypeObject* NVNameError_Type = NULL;
NvTypeObject* NVAssertionError_Type = NULL;

void initialize_exception_types(void) {
    // Criar tipo base Error
    NVError_Type = nv_type_new("Error", NULL, 0);
    
    // Criar tipos específicos de exceção que herdam de Error
    NvTypeObject* error_bases[] = {NVError_Type};
    
    NVValueError_Type = nv_type_new("ValueError", error_bases, 1);
    NVTypeError_Type = nv_type_new("TypeError", error_bases, 1);
    NVRuntimeError_Type = nv_type_new("RuntimeError", error_bases, 1);
    NVIndexError_Type = nv_type_new("IndexError", error_bases, 1);
    NVKeyError_Type = nv_type_new("KeyError", error_bases, 1);
    NVAttributeError_Type = nv_type_new("AttributeError", error_bases, 1);
    NVNameError_Type = nv_type_new("NameError", error_bases, 1);
    NVAssertionError_Type = nv_type_new("AssertionError", error_bases, 1);
    
    // Configurar destrutores para exceções
    if (NVError_Type) {
        NVError_Type->tp_dealloc = (void (*)(struct NvObject*))free;
    }
    
    // Configurar destrutores para tipos específicos
    NvTypeObject* exception_types[] = {
        NVValueError_Type, NVTypeError_Type, NVRuntimeError_Type,
        NVIndexError_Type, NVKeyError_Type, NVAttributeError_Type,
        NVNameError_Type, NVAssertionError_Type
    };
    
    for (int i = 0; i < 8; i++) {
        if (exception_types[i]) {
            exception_types[i]->tp_dealloc = (void (*)(struct NvObject*))free;
        }
    }
}
