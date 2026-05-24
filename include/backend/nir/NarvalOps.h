#pragma once

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Interfaces/InferTypeOpInterface.h"

#include "backend/nir/NarvalDialect.h"
#include "backend/nir/NarvalTypes.h"
#include "backend/nir/NarvalAttrs.h"

#define GET_OP_CLASSES
#include "NarvalOps.h.inc"
