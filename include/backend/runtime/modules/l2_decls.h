// Gerado a partir de l2.def, no modo "declaração". Nada aqui é escrito à mão.
#ifndef NV_L2_DECLS_H
#define NV_L2_DECLS_H

#include "backend/runtime/nv_runtime.h"

#define bytes const char*
#define num   int
#define big   long long
#define NV_RET_bytes const char*
#define NV_RET_num   int
#define NV_RET_big   long long
#define NV_RET_flag  int
#define NV_FN(name, params, ret, arity) NV_RET_##ret nv_l2_##name params;
#include "backend/runtime/modules/l2.def"
#undef NV_FN
#undef NV_RET_bytes
#undef NV_RET_num
#undef NV_RET_big
#undef NV_RET_flag
#undef bytes
#undef num
#undef big

#endif
