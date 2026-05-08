#include "backend/runtime/prototypes.h"
#include "backend/runtime/string_concat.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

char* string_concat(char* str1, char* str2) {
    if (!str1) str1 = "";
    if (!str2) str2 = "";
    size_t len1 = strlen(str1);
    size_t len2 = strlen(str2);
    char* result = (char*)malloc(len1 + len2 + 1);
    if (!result) return NULL;
    strcpy(result, str1);
    strcat(result, str2);
    return result;
}
