#pragma once

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/NoFolder.h>
#include <vector>
#include <memory>
#include "frontend/ast/ast.hpp"
#include "backend/codegen/ir_context.hpp"

namespace rph {

void generate_ir(std::unique_ptr<Node> node, IRGenerationContext& context);

} // namespace rph