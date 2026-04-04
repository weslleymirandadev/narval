#include "backend/runtime/prototypes.h"
#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

// Sistema de sobrecarga de operadores como Python __add__

// Verifica se um tipo tem método __add__ definido
int has_add_method(NvTypeObject* type) {
    if (!type) return 0;
    
    // Para tipos básicos, temos comportamentos definidos
    if (type == NVStr_Type) return 1;  // String tem __add__ para concatenação
    if (type == NVInt_Type) return 1;  // Int tem __add__ para soma
    if (type == NVFloat_Type) return 1; // Float tem __add__ para soma
    
    // TODO: Para tipos customizados, verificar na tabela de métodos
    return 0;
}

// Implementação do __add__ para diferentes tipos
int binary_add(Value* result, Value* lhs, Value* rhs) {
    printf("[DEBUG] binary_add called\n");
    if (!result || !lhs || !rhs || !lhs->obj || !rhs->obj) {
        printf("[DEBUG] binary_add: null pointer check failed\n");
        return 0; // Erro
    }
    
    // Limpar resultado para evitar problemas
    memset(result, 0, sizeof(Value));
    
    NvTypeObject* lhs_type = lhs->obj->ob_type;
    NvTypeObject* rhs_type = rhs->obj->ob_type;
    
    printf("[DEBUG] binary_add: lhs_type=%p, rhs_type=%p\n", (void*)lhs_type, (void*)rhs_type);
    printf("[DEBUG] binary_add: NVStr_Type=%p, NVInt_Type=%p, NVFloat_Type=%p\n", 
           (void*)NVStr_Type, (void*)NVInt_Type, (void*)NVFloat_Type);
    
    // Verificação de segurança - só operar se ambos tiverem tipos
    if (!lhs_type || !rhs_type) {
        printf("[DEBUG] binary_add: missing types, returning error\n");
        return 0;
    }
    
    // Caso 1: String + qualquer coisa -> concatenação
    if (lhs_type == NVStr_Type) {
        printf("[DEBUG] binary_add: lhs is string\n");
        char* lhs_str = ((NVStr*)lhs->obj)->value;
        if (!lhs_str) lhs_str = "";
        printf("[DEBUG] binary_add: lhs_str='%s'\n", lhs_str);
        
        if (rhs_type == NVStr_Type) {
            printf("[DEBUG] binary_add: string + string\n");
            // String + String
            char* rhs_str = ((NVStr*)rhs->obj)->value;
            if (!rhs_str) rhs_str = "";
            printf("[DEBUG] binary_add: rhs_str='%s'\n", rhs_str);
            char* result_str = string_concat(lhs_str, rhs_str);
            printf("[DEBUG] binary_add: result_str='%s'\n", result_str);
            create_str(result, result_str);
            if (result_str) free(result_str);
            return 1;
        } else if (rhs_type == NVInt_Type) {
            printf("[DEBUG] binary_add: string + int\n");
            // String + Int -> converter int para string e concatenar
            int32_t rhs_int = ((NVInt*)rhs->obj)->value;
            printf("[DEBUG] binary_add: rhs_int=%d\n", rhs_int);
            Value temp_str;
            memset(&temp_str, 0, sizeof(Value));
            int_to_string(&temp_str, rhs_int);
            printf("[DEBUG] binary_add: converted int to string\n");
            char* rhs_str = extract_string_from_value(&temp_str);
            printf("[DEBUG] binary_add: extracted rhs_str='%s'\n", rhs_str);
            char* result_str = string_concat(lhs_str, rhs_str);
            printf("[DEBUG] binary_add: result_str='%s'\n", result_str);
            create_str(result, result_str);
            if (result_str) free(result_str);
            return 1;
        } else if (rhs_type == NVFloat_Type) {
            printf("[DEBUG] binary_add: string + float\n");
            // String + Float -> converter float para string e concatenar
            double rhs_float = ((NVFloat*)rhs->obj)->value;
            printf("[DEBUG] binary_add: rhs_float=%f\n", rhs_float);
            Value temp_str;
            memset(&temp_str, 0, sizeof(Value));
            float_to_string(&temp_str, rhs_float);
            printf("[DEBUG] binary_add: converted float to string\n");
            char* rhs_str = extract_string_from_value(&temp_str);
            printf("[DEBUG] binary_add: extracted rhs_str='%s'\n", rhs_str);
            char* result_str = string_concat(lhs_str, rhs_str);
            printf("[DEBUG] binary_add: result_str='%s'\n", result_str);
            create_str(result, result_str);
            if (result_str) free(result_str);
            return 1;
        }
    }
    
    // Caso 2: Int + String -> concatenação (Python behavior)
    if (lhs_type == NVInt_Type && rhs_type == NVStr_Type) {
        printf("[DEBUG] binary_add: int + string\n");
        int32_t lhs_int = ((NVInt*)lhs->obj)->value;
        char* rhs_str = ((NVStr*)rhs->obj)->value;
        if (!rhs_str) rhs_str = "";
        printf("[DEBUG] binary_add: lhs_int=%d, rhs_str='%s'\n", lhs_int, rhs_str);
        
        // Converter int para string
        Value temp_str;
        memset(&temp_str, 0, sizeof(Value));  // Inicializar para evitar lixo
        printf("[DEBUG] binary_add: calling int_to_string\n");
        int_to_string(&temp_str, lhs_int);
        printf("[DEBUG] binary_add: int_to_string returned\n");
        char* lhs_str = extract_string_from_value(&temp_str);
        printf("[DEBUG] binary_add: extracted lhs_str='%s'\n", lhs_str);
        
        printf("[DEBUG] binary_add: calling string_concat\n");
        char* result_str = string_concat(lhs_str, rhs_str);
        printf("[DEBUG] binary_add: string_concat returned result_str='%s'\n", result_str);
        create_str(result, result_str);
        if (result_str) free(result_str);
        printf("[DEBUG] binary_add: int + string completed\n");
        return 1;
    }
    
    // Caso 3: Float + String -> concatenação
    if (lhs_type == NVFloat_Type && rhs_type == NVStr_Type) {
        double lhs_float = ((NVFloat*)lhs->obj)->value;
        char* rhs_str = ((NVStr*)rhs->obj)->value;
        
        // Converter float para string
        Value temp_str;
        memset(&temp_str, 0, sizeof(Value));  // Inicializar para evitar lixo
        float_to_string(&temp_str, lhs_float);
        char* lhs_str = extract_string_from_value(&temp_str);
        
        char* result_str = string_concat(lhs_str, rhs_str);
        create_str(result, result_str);
        if (result_str) free(result_str);
        return 1;
    }
    
    // Caso 4: Int + Int -> soma
    if (lhs_type == NVInt_Type && rhs_type == NVInt_Type) {
        int32_t lhs_int = ((NVInt*)lhs->obj)->value;
        int32_t rhs_int = ((NVInt*)rhs->obj)->value;
        int32_t result_int = lhs_int + rhs_int;
        create_int(result, result_int);
        return 1;
    }
    
    // Caso 5: Float + Float -> soma
    if (lhs_type == NVFloat_Type && rhs_type == NVFloat_Type) {
        double lhs_float = ((NVFloat*)lhs->obj)->value;
        double rhs_float = ((NVFloat*)rhs->obj)->value;
        double result_float = lhs_float + rhs_float;
        create_float(result, result_float);
        return 1;
    }
    
    // Caso 6: Int + Float -> soma (promover para float)
    if (lhs_type == NVInt_Type && rhs_type == NVFloat_Type) {
        double lhs_float = (double)((NVInt*)lhs->obj)->value;
        double rhs_float = ((NVFloat*)rhs->obj)->value;
        double result_float = lhs_float + rhs_float;
        create_float(result, result_float);
        return 1;
    }
    
    // Caso 7: Float + Int -> soma (promover para float)
    if (lhs_type == NVFloat_Type && rhs_type == NVInt_Type) {
        double lhs_float = ((NVFloat*)lhs->obj)->value;
        double rhs_float = (double)((NVInt*)rhs->obj)->value;
        double result_float = lhs_float + rhs_float;
        create_float(result, result_float);
        return 1;
    }
    
    // TODO: Para tipos customizados, chamar método __add__
    
    return 0; // Operação não suportada
}
