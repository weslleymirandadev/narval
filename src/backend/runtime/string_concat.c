#include "backend/runtime/prototypes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Função de concatenação de strings - aloca memória e retorna resultado
char* string_concat(const char* str1, const char* str2) {
    printf("[DEBUG] string_concat called with str1='%s', str2='%s'\n", str1 ? str1 : "NULL", str2 ? str2 : "NULL");
    
    if (!str1) str1 = "";
    if (!str2) str2 = "";
    
    size_t len1 = strlen(str1);
    size_t len2 = strlen(str2);
    
    printf("[DEBUG] string_concat: len1=%zu, len2=%zu\n", len1, len2);
    
    char* result = (char*)malloc(len1 + len2 + 1);
    if (!result) {
        printf("[DEBUG] string_concat: malloc failed\n");
        return NULL;
    }
    
    strcpy(result, str1);
    strcat(result, str2);
    
    printf("[DEBUG] string_concat: result='%s'\n", result);
    return result;
}
