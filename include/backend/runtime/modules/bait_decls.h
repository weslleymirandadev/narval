// Gerado a partir de bait.def, no modo "declaração". Nada aqui é escrito à mão.
#ifndef NV_BAIT_DECLS_H
#define NV_BAIT_DECLS_H

#include "backend/runtime/nv_runtime.h"

#define bytes const char*
#define num   int
#define NV_RET_bytes const char*
#define NV_RET_num   int
#define NV_RET_flag  int
#define NV_FN(name, params, ret, arity) NV_RET_##ret nv_bait_##name params;
#include "backend/runtime/modules/bait.def"
#undef NV_FN
#undef NV_RET_bytes
#undef NV_RET_num
#undef NV_RET_flag
#undef bytes
#undef num

#endif
