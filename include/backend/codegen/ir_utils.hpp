#pragma once

#include "backend/codegen/ir_context.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/checker/type.hpp"
#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <memory>
#include <vector>
#include <string>

namespace nv {
namespace ir_utils {

struct IfElseBlocks {
    llvm::BasicBlock* then_block;
    llvm::BasicBlock* else_block;
    llvm::BasicBlock* merge_block;
};

// === Constantes ===
llvm::Value* create_string_constant(IRGenerationContext& context, const std::string& value);
llvm::Value* create_int_constant(IRGenerationContext& context, int32_t value);
llvm::Value* create_float_constant(IRGenerationContext& context, double value);
llvm::Value* create_bool_constant(IRGenerationContext& context, bool value);

// === Coerção ===
llvm::Value* promote_type(IRGenerationContext& context, llvm::Value* value, llvm::Type* target_type);
llvm::Value* promote_nv_type(
    IRGenerationContext& context,
    llvm::Value* value,
    std::shared_ptr<Type> source_type,
    std::shared_ptr<Type> target_type
);

// === Operações ===
llvm::Value* create_comparison(
    IRGenerationContext& context,
    llvm::Value* lhs,
    llvm::Value* rhs,
    const std::string& op
);
llvm::Value* create_binary_op(
    IRGenerationContext& context,
    llvm::Value* lhs,
    llvm::Value* rhs,
    const std::string& op
);
llvm::Value* create_unary_op(
    IRGenerationContext& context,
    llvm::Value* operand,
    const std::string& op
);

// === Inferência (APENAS com Type) ===
llvm::Type* infer_llvm_type(IRGenerationContext& context, Type& type);

// === Controle de fluxo ===
llvm::BasicBlock* create_and_set_block(IRGenerationContext& context, const std::string& name);
llvm::Function* create_function(
    IRGenerationContext& context,
    const std::string& name,
    std::shared_ptr<Label>& label_type
);
llvm::Function* create_function(
    IRGenerationContext& context,
    const std::string& name,
    llvm::Type* return_type,
    const std::vector<llvm::Type*>& param_types,
    bool is_vararg = false
);
llvm::Value* create_function_call(
    IRGenerationContext& context,
    llvm::Function* function,
    const std::vector<llvm::Value*>& args
);
llvm::ReturnInst* create_return(IRGenerationContext& context, llvm::Value* value = nullptr);
void create_conditional_branch(
    IRGenerationContext& context,
    llvm::Value* condition,
    llvm::BasicBlock* then_block,
    llvm::BasicBlock* else_block
);
void create_unconditional_branch(IRGenerationContext& context, llvm::BasicBlock* target_block);
IfElseBlocks create_if_else_structure(IRGenerationContext& context, const std::string& base_name = "if");
void finalize_if_else(IRGenerationContext& context, const IfElseBlocks& blocks, bool has_else = false);

// === Helpers LLVM ===
llvm::Type* get_i32(IRGenerationContext& ctx);
llvm::Type* get_i64(IRGenerationContext& ctx);
llvm::Type* get_f64(IRGenerationContext& ctx);
llvm::Type* get_i1(IRGenerationContext& ctx);
llvm::Type* get_void(IRGenerationContext& ctx);
llvm::Type* get_i8(IRGenerationContext& ctx);
llvm::Type* get_i8_ptr(IRGenerationContext& ctx);
llvm::StructType* get_value_struct(IRGenerationContext& ctx);
llvm::PointerType* get_value_ptr(IRGenerationContext& ctx);
// Cria uma constante Value com a tag de tipo correta para inicialização de GlobalVariables
llvm::Constant* create_value_constant_with_tag(IRGenerationContext& ctx, int32_t tag, int64_t value);
// Cria uma constante Value genérica baseada no tipo inferido e valor LLVM
// Retorna nullptr se não for possível criar uma constante (deve usar runtime init)
llvm::Constant* create_value_constant_from_llvm_value(
    IRGenerationContext& ctx,
    llvm::Value* value,
    std::shared_ptr<Type> nv_type = nullptr
);

// === String → LLVM Type (completo) ===
llvm::Type* llvm_type_from_string(IRGenerationContext& ctx, const std::string& type_str);
static llvm::Type* parse_type_recursive(const std::string& s, size_t& p, IRGenerationContext& ctx);

} // namespace ir_utils
} // namespace nv