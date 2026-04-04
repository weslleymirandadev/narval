#include "backend/runtime/prototypes.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Função helper para int + string concatenação
char* int_string_concat(int32_t value, const char* str) {
    // Versão super minimalista para debug
    static char result[64];  // Buffer estático para evitar malloc
    
    // Converter int para string manualmente
    if (value == 0) {
        result[0] = '0';
        result[1] = '\0';
    } else {
        int i = 0;
        int32_t temp = value;
        if (temp < 0) {
            result[i++] = '-';
            temp = -temp;
        }
        
        // Converter digitos
        char digits[32];
        int digit_count = 0;
        while (temp > 0 && digit_count < 31) {
            digits[digit_count++] = '0' + (temp % 10);
            temp /= 10;
        }
        
        // Inverter digitos
        for (int j = digit_count - 1; j >= 0; j--) {
            result[i++] = digits[j];
        }
        result[i] = '\0';
    }
    
    // Concatenar com str (versão segura)
    if (str) {
        int len = strlen(result);
        int i = len;
        while (i < 62 && *str && i < 62) {
            result[i++] = *str++;
        }
        result[i] = '\0';
    }
    
    return result;
}

// Função helper para string + int concatenação
char* string_int_concat(const char* str, int32_t value) {
    // Versão super minimalista para debug
    static char result[64];  // Buffer estático para evitar malloc
    
    // Copiar string primeiro
    if (str) {
        int i = 0;
        while (i < 31 && *str && i < 31) {
            result[i++] = *str++;
        }
        result[i] = '\0';
    } else {
        result[0] = '\0';
    }
    
    // Converter int para string e concatenar
    if (value == 0) {
        int len = strlen(result);
        result[len] = '0';
        result[len + 1] = '\0';
    } else {
        int len = strlen(result);
        int i = len;
        int32_t temp = value;
        if (temp < 0) {
            result[i++] = '-';
            temp = -temp;
        }
        
        // Converter digitos
        char digits[32];
        int digit_count = 0;
        while (temp > 0 && digit_count < 31) {
            digits[digit_count++] = '0' + (temp % 10);
            temp /= 10;
        }
        
        // Inverter digitos
        for (int j = digit_count - 1; j >= 0; j--) {
            result[i++] = digits[j];
        }
        result[i] = '\0';
    }
    
    return result;
}
