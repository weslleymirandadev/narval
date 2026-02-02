#include "backend/runtime/nv_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================= */
/*                    RASTREIO E VALIDAÇÃO DE TIPOS             */
/* ============================================================= */

// Garantir que um Value tenha a tag correta (com rastreio melhorado)
void ensure_value_type(Value* v) {
    if (!v) return;
    
    // Se o tipo já está definido e válido, verificar consistência
    if (v->type >= TAG_INT && v->type <= TAG_ANY) {
        // Validar consistência com prototype
        switch (v->type) {
            case TAG_STR:
                if (v->prototype != string_prototype) {
                    v->prototype = string_prototype;
                }
                break;
            case TAG_ARRAY:
                if (v->prototype != array_prototype) {
                    v->prototype = array_prototype;
                }
                break;
            case TAG_VECTOR:
                if (v->prototype != vector_prototype) {
                    v->prototype = vector_prototype;
                }
                break;
            case TAG_MAP:
                if (v->prototype != map_prototype) {
                    v->prototype = map_prototype;
                }
                break;
        }
        return;
    }
    
    // Se o tipo é customizado, verificar se está registrado
    if (v->type >= TAG_CUSTOM) {
        TypeInfo* info = get_type_info(v->type);
        if (!info) {
            // Tipo não encontrado, tentar inferir
            v->type = TAG_ANY;
            v->type_info = NULL;
        } else {
            v->type_info = info;
        }
        return;
    }
    
    // Se o tipo é 0 ou inválido, tentar inferir
    // IMPORTANTE: Não inferir tipo para valores que já têm tipo definido (incluindo TAG_FLOAT)
    // A inferência só deve acontecer quando o tipo é realmente desconhecido (0 ou inválido)
    if (v->type == 0 || (v->type > TAG_ANY && v->prototype == NULL)) {
        // Inferir tipo baseado em prototype
        if (v->prototype == string_prototype) {
            v->type = TAG_STR;
        } else if (v->prototype == array_prototype) {
            v->type = TAG_ARRAY;
        } else if (v->prototype == vector_prototype) {
            v->type = TAG_VECTOR;
        } else if (v->prototype == map_prototype) {
            v->type = TAG_MAP;
        } else if (v->prototype == NULL) {
            // IMPORTANTE: Não podemos inferir se é int ou float apenas pelo valor numérico
            // Um float pode ter um valor que, quando interpretado como int64, está dentro do range de int32
            // Portanto, se não temos informação de tipo, não devemos assumir que é int
            // Deixar como TAG_ANY para evitar conversão incorreta
            // A inferência de tipo primitivo só deve acontecer quando temos certeza absoluta
            v->type = TAG_ANY;
        } else {
            v->type = TAG_ANY;
        }
    }
}

// Validar tipo de um Value
int validate_value_type(const Value* v) {
    if (!v) return 0;
    
    // Verificar se o tipo é válido
    if (!is_valid_type(v->type)) {
        return 0;
    }
    
    // Validações específicas por tipo
    switch (v->type) {
        case TAG_INT:
        case TAG_FLOAT:
        case TAG_BOOL:
            // Tipos primitivos sempre válidos se a tag está correta
            return 1;
            
        case TAG_STR:
            // String deve ter prototype correto ou ponteiro válido
            return v->prototype == string_prototype || v->value != 0;
            
        case TAG_ARRAY:
        case TAG_VECTOR:
        case TAG_MAP:
        case TAG_TUPLE:
            // Estruturas devem ter ponteiro válido
            return v->value != 0;
            
        case TAG_ANY:
            return 1; // Any aceita qualquer coisa
            
        case TAG_CUSTOM:
        default:
            // Tipos customizados devem ter type_info válido
            if (v->type >= TAG_CUSTOM) {
                TypeInfo* info = get_type_info(v->type);
                return info != NULL && v->value != 0;
            }
            return 0;
    }
}

// Obter tipo de um Value (com validação)
int32_t get_value_type(const Value* v) {
    if (!v) return 0;
    
    if (validate_value_type(v)) {
        return v->type;
    }
    
    // Se inválido, tentar garantir tipo
    Value* mutable_v = (Value*)v;
    ensure_value_type(mutable_v);
    return mutable_v->type;
}

// Verificar compatibilidade de tipos
int types_compatible(int32_t type1, int32_t type2) {
    if (type1 == type2) return 1;
    if (type1 == TAG_ANY || type2 == TAG_ANY) return 1;
    
    // Compatibilidade numérica
    if ((type1 == TAG_INT || type1 == TAG_FLOAT) &&
        (type2 == TAG_INT || type2 == TAG_FLOAT)) {
        return 1;
    }
    
    // Verificar herança para tipos customizados
    if (type1 >= TAG_CUSTOM && type2 >= TAG_CUSTOM) {
        TypeInfo* info1 = get_type_info(type1);
        TypeInfo* info2 = get_type_info(type2);
        if (info1 && info2) {
            // Verificar se type2 é base de type1
            TypeInfo* base = info1->base_type;
            while (base) {
                if (base->type_id == type2) return 1;
                base = base->base_type;
            }
        }
    }
    
    return 0;
}

// Converter tipo (se possível)
int convert_type(Value* out, const Value* in, int32_t target_type) {
    if (!out || !in) return 0;
    
    if (in->type == target_type) {
        *out = *in;
        return 1;
    }
    
    // Conversões numéricas
    if (target_type == TAG_INT) {
        if (in->type == TAG_FLOAT) {
            create_int(out, (int32_t)in->value);
            return 1;
        }
        if (in->type == TAG_BOOL) {
            create_int(out, in->value ? 1 : 0);
            return 1;
        }
    }
    
    if (target_type == TAG_FLOAT) {
        if (in->type == TAG_INT) {
            double d = (double)in->value;
            create_float(out, d);
            return 1;
        }
    }
    
    if (target_type == TAG_BOOL) {
        if (in->type == TAG_INT) {
            create_bool(out, in->value != 0);
            return 1;
        }
    }
    
    return 0;
}

// Obter informações de tipo de um Value
TypeInfo* get_value_type_info(const Value* v) {
    if (!v) return NULL;
    
    if (v->type >= TAG_CUSTOM) {
        return get_type_info(v->type);
    }
    
    return NULL; // Tipos builtin não têm TypeInfo completo
}

// Imprimir informações de tipo (para debug)
void print_type_info(const Value* v) {
    if (!v) {
        printf("Value: NULL\n");
        return;
    }
    
    printf("Value type: %s (id: %d)\n", get_type_name(v->type), v->type);
    printf("  Prototype: %p\n", v->prototype);
    printf("  Type info: %p\n", v->type_info);
    printf("  Flags: 0x%x\n", v->flags);
    
    if (v->type >= TAG_CUSTOM) {
        TypeInfo* info = get_type_info(v->type);
        if (info) {
            printf("  Type name: %s\n", info->type_name);
            printf("  Size: %zu bytes\n", info->size);
        }
    }
}

/* ============================================================= */
/*                    GARBAGE COLLECTION                        */
/* ============================================================= */

// Marcar valor para GC (se necessário)
void gc_mark_value(const Value* v) {
    if (!v) return;
    
    // Implementação básica - pode ser expandida
    // Por enquanto, apenas tipos com ponteiros precisam de marcação
    switch (v->type) {
        case TAG_STR:
        case TAG_ARRAY:
        case TAG_VECTOR:
        case TAG_MAP:
        case TAG_TUPLE:
        case TAG_CUSTOM:
            // Estes tipos têm ponteiros que precisam ser marcados
            // Implementação específica depende do GC usado
            break;
        default:
            break;
    }
}

// Liberar valor (chama destructor se necessário)
void free_value(Value* v) {
    if (!v) return;
    
    // Chamar destructor se for tipo customizado
    if (v->type >= TAG_CUSTOM) {
        TypeInfo* info = get_type_info(v->type);
        if (info) {
            // Se é struct-like, liberar campos primeiro
            if (info->field_names && info->field_count > 0 && v->value != 0) {
                Value* fields = (Value*)(intptr_t)v->value;
                for (int i = 0; i < info->field_count; i++) {
                    // Liberar campos recursivamente se necessário
                    if (fields[i].type >= TAG_STR) {
                        free_value(&fields[i]);
                    }
                }
            }
            // Chamar destructor customizado se disponível
            if (info->destructor && v->value != 0) {
                info->destructor((void*)(intptr_t)v->value);
            } else if (v->value != 0) {
                // Se não tem destructor, apenas liberar memória
                free((void*)(intptr_t)v->value);
            }
        }
    }
    
    // Limpar valor
    v->type = 0;
    v->value = 0;
    v->prototype = NULL;
    v->type_info = NULL;
    v->flags = 0;
}
