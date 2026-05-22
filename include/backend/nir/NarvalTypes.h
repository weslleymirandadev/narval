#pragma once

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/TypeSupport.h"
#include "mlir/IR/Types.h"

#include "backend/nir/NarvalDialect.h"

#define GET_TYPEDEF_CLASSES
#include "NarvalTypes.h.inc"
