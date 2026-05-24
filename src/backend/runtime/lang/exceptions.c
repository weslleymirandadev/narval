#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>

/* Tipos de exceção globais (definidos em builtin_classes.c) */
extern NvTypeObject* NVError_Type;
extern NvTypeObject* NVValueError_Type;
extern NvTypeObject* NVTypeError_Type;
extern NvTypeObject* NVRuntimeError_Type;
extern NvTypeObject* NVIndexError_Type;
extern NvTypeObject* NVKeyError_Type;
extern NvTypeObject* NVAttributeError_Type;
extern NvTypeObject* NVNameError_Type;
extern NvTypeObject* NVAssertionError_Type;

/* ============================================================= */
/*              SISTEMA DE EXCEPTION HANDLING (SETJMP)           */
/* ============================================================= */

#define NV_MAX_HANDLER_DEPTH 128

typedef struct {
    jmp_buf buf;
} NvExcHandler;

static NvExcHandler nv_handler_stack[NV_MAX_HANDLER_DEPTH];
static int nv_handler_depth = 0;

static Value nv_current_exception;
static int nv_has_exception = 0;

void* nv_push_try_handler(void) {
    if (nv_handler_depth >= NV_MAX_HANDLER_DEPTH) {
        fprintf(stderr, "narval: exception handler stack overflow\n");
        exit(1);
    }
    return (void*)&nv_handler_stack[nv_handler_depth++].buf;
}

void nv_pop_try_handler(void) {
    if (nv_handler_depth > 0) nv_handler_depth--;
}

void nv_get_current_exception_into(Value* out) {
    if (!out) return;
    if (nv_has_exception) {
        *out = nv_current_exception;
    } else {
        out->obj = NULL;
    }
}

void nv_get_exception_message_into(Value* out) {
    if (!out) return;
    const char* msg = "";
    if (nv_has_exception && nv_current_exception.obj) {
        NVError* err = (NVError*)nv_current_exception.obj;
        if (err->message) msg = err->message;
    }
    create_str(out, msg);
}

const char* nv_get_exception_type_name(void) {
    if (!nv_has_exception || !nv_current_exception.obj) return NULL;
    NvTypeObject* type = nv_current_exception.obj->ob_type;
    return type ? type->tp_name : NULL;
}

int nv_exception_matches(const char* type_name) {
    if (!nv_has_exception || !type_name) return 0;
    /* "Error" funciona como catch-all */
    if (strcmp(type_name, "Error") == 0) return 1;
    const char* exc_type = nv_get_exception_type_name();
    if (!exc_type) return 0;
    return strcmp(exc_type, type_name) == 0;
}

void nv_clear_current_exception(void) {
    nv_has_exception = 0;
    nv_current_exception.obj = NULL;
}

void nv_rethrow_current_exception(void) {
    if (nv_has_exception) {
        nv_throw_exception(&nv_current_exception);
    }
}

/* ============================================================= */
/*                    CRIACAO DE EXCECOES                        */
/* ============================================================= */

static void create_exception_base(Value* out, NvTypeObject* type, const char* message) {
    if (!out || !type) return;

    NVError* exception = (NVError*)calloc(1, sizeof(NVError));
    if (!exception) return;

    exception->ob_base.ob_type = type;
    exception->ob_base.ref_count = 1;
    exception->ob_base.flags = 0;
    exception->message = strdup(message ? message : "");
    exception->traceback = NULL;

    out->obj = (NvObject*)exception;
}

void create_error(Value* out, const char* message) {
    create_exception_base(out, NVError_Type, message);
}

void create_value_error(Value* out, const char* message) {
    create_exception_base(out, NVValueError_Type, message);
}

void create_type_error(Value* out, const char* message) {
    create_exception_base(out, NVTypeError_Type, message);
}

void create_runtime_error(Value* out, const char* message) {
    create_exception_base(out, NVRuntimeError_Type, message);
}

void create_index_error(Value* out, const char* message) {
    create_exception_base(out, NVIndexError_Type, message);
}

void create_key_error(Value* out, const char* message) {
    create_exception_base(out, NVKeyError_Type, message);
}

void create_attribute_error(Value* out, const char* message) {
    create_exception_base(out, NVAttributeError_Type, message);
}

void create_name_error(Value* out, const char* message) {
    create_exception_base(out, NVNameError_Type, message);
}

void create_assertion_error(Value* out, const char* message) {
    create_exception_base(out, NVAssertionError_Type, message);
}

void nv_create_exception(Value* out, const char* type_name, const char* message) {
    if (!type_name) { create_error(out, message); return; }
    if      (strcmp(type_name, "ValueError")     == 0) create_value_error(out, message);
    else if (strcmp(type_name, "TypeError")      == 0) create_type_error(out, message);
    else if (strcmp(type_name, "RuntimeError")   == 0) create_runtime_error(out, message);
    else if (strcmp(type_name, "IndexError")     == 0) create_index_error(out, message);
    else if (strcmp(type_name, "KeyError")       == 0) create_key_error(out, message);
    else if (strcmp(type_name, "AttributeError") == 0) create_attribute_error(out, message);
    else if (strcmp(type_name, "NameError")      == 0) create_name_error(out, message);
    else if (strcmp(type_name, "AssertionError") == 0) create_assertion_error(out, message);
    else create_error(out, message);
}

const char* nv_extract_string_ptr(Value* v) {
    if (!v || !v->obj) return "";
    NVStr* str = (NVStr*)v->obj;
    return str->value ? str->value : "";
}

/* ============================================================= */
/*                    LANCAMENTO DE EXCECOES                     */
/* ============================================================= */

void nv_save_exception(Value* exception) {
    if (!exception || !exception->obj) return;
    nv_current_exception = *exception;
    nv_has_exception = 1;
}

void nv_throw_exception(Value* exception) {
    if (!exception || !exception->obj) return;

    /* Salvar excecao no estado global antes do longjmp */
    nv_current_exception = *exception;
    nv_has_exception = 1;

    /* Construir traceback se ainda nao existir */
    NVError* err = (NVError*)exception->obj;
    if (!err->traceback) {
        char buffer[2048];
        int pos = snprintf(buffer, sizeof(buffer), "Traceback (most recent call last):\n");
        for (int i = nv_trace_depth - 1; i >= 0 && pos < (int)sizeof(buffer) - 1; i--) {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                "  File \"%s\", line %d, in %s\n",
                nv_trace_stack[i].file ? nv_trace_stack[i].file : "<unknown>",
                nv_trace_stack[i].line,
                nv_trace_stack[i].func ? nv_trace_stack[i].func : "<unknown>");
        }
        err->traceback = strdup(buffer);
        if (nv_current_exception.obj)
            ((NVError*)nv_current_exception.obj)->traceback = err->traceback;
    }

    /* Se ha um handler ativo, fazer longjmp para ele */
    if (nv_handler_depth > 0) {
        nv_handler_depth--;
        longjmp(nv_handler_stack[nv_handler_depth].buf, 1);
        /* NOT REACHED */
    }

    /* Sem handler: excecao nao capturada */
    NvTypeObject* type = exception->obj->ob_type;
    const char* type_name = (type && type->tp_name) ? type->tp_name : "Error";
    fprintf(stderr, "%s: %s\n", type_name, err->message ? err->message : "");
    if (err->traceback) {
        fprintf(stderr, "%s", err->traceback);
    }
    exit(1);
}

void nv_raise_exception(const char* exception_type, const char* message) {
    Value exception;
    exception.obj = NULL;
    nv_create_exception(&exception, exception_type, message);
    nv_throw_exception(&exception);
}

/* ============================================================= */
/*                    UTILITARIOS DE EXCECAO                     */
/* ============================================================= */

int is_exception(Value* v) {
    if (!v || !v->obj) return 0;
    NvTypeObject* type = v->obj->ob_type;
    if (!type) return 0;

    extern NvTypeObject* NVError_Type;
    if (type == NVError_Type) return 1;

    const char* type_name = type->tp_name;
    if (!type_name) return 0;
    size_t len = strlen(type_name);
    if (len > 5 && strcmp(type_name + len - 5, "Error") == 0) return 1;
    return 0;
}

char* get_exception_message(Value* exception) {
    if (!is_exception(exception)) return NULL;
    NVError* err = (NVError*)exception->obj;
    return err->message ? strdup(err->message) : strdup("");
}
