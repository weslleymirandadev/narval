// Gerado a partir de keyboard.def, no modo "declaração".
#ifndef NV_KEYBOARD_DECLS_H
#define NV_KEYBOARD_DECLS_H

#include "backend/runtime/nv_runtime.h"

#define bytes const char*
#define num   int
#define big   long long
#define NV_RET_bytes const char*
#define NV_RET_num   int
#define NV_RET_big   long long
#define NV_RET_flag  int
#define NV_FN(name, params, ret, arity) NV_RET_##ret nv_keyboard_##name params;
#include "backend/runtime/modules/keyboard.def"
#undef NV_FN
#undef NV_RET_bytes
#undef NV_RET_num
#undef NV_RET_big
#undef NV_RET_flag
#undef bytes
#undef num
#undef big

#endif
