#include "backend/runtime/prototypes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Função de concatenação de strings - aloca memória e retorna resultado
char* string_concat(const char* str1, const char* str2) {
    if (!str1) str1 = "";
    if (!str2) str2 = "";
    
    size_t len1 = strlen(str1);
    size_t len2 = strlen(str2);
    
    // Alocar memória para resultado
    char* result = malloc(len1 + len2 + 1);
    if (!result) return NULL;
    
    // Copiar primeira string
    strcpy(result, str1);
    // Concatenar segunda string
    strcat(result, str2);
    
    return result;
}
