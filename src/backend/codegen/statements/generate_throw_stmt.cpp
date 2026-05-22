#include "frontend/ast/statements/throw_stmt_node.hpp"
#include "frontend/ast/expressions/call_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "backend/codegen/generate_ir.hpp"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

static const char* exc_create_fn(const std::string& type_name) {
    if (type_name == "ValueError")     return "create_value_error";
    if (type_name == "TypeError")      return "create_type_error";
    if (type_name == "RuntimeError")   return "create_runtime_error";
    if (type_name == "IndexError")     return "create_index_error";
    if (type_name == "KeyError")       return "create_key_error";
    if (type_name == "AttributeError") return "create_attribute_error";
    if (type_name == "NameError")      return "create_name_error";
    if (type_name == "AssertionError") return "create_assertion_error";
    if (type_name == "Error")          return "create_error";
    return nullptr;
}

void ThrowStatementNode::codegen(nv::IRGenerationContext& ctx) {
    if (nv::get_feature_tracker().no_std) return;
    auto& B     = ctx.get_builder();
    auto* ValueTy  = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
    auto* I8Ptr    = llvm::PointerType::getUnqual(ctx.get_context());

    auto* exc_alloca = ctx.create_alloca(ValueTy, "throw_exc");
    bool created = false;

    /*  Caso 1: throw ExceptionType("msg")  */
    if (exception->kind == NodeType::CallExpression) { auto* call = static_cast<CallExprNode*>(exception.get());
        if (call->caller->kind == NodeType::Identifier) { auto* id = static_cast<IdentifierNode*>(call->caller.get());
            const char* create_name = exc_create_fn(id->symbol);
            if (create_name) {
                /* Avaliar mensagem (primeiro argumento) */
                llvm::Value* msg_ptr = B.CreateGlobalString("", "default_msg");
                if (!call->args.empty()) {
                    if (call->args[0]->value) call->args[0]->value->codegen(ctx);
                    if (ctx.has_value()) {
                        auto* msg_val = ctx.pop_value();
                        if (msg_val) {
                            if (msg_val->getType()->isPointerTy()) {
                                /* String literal → i8* direto */
                                msg_ptr = msg_val;
                            } else if (msg_val->getType() == ValueTy) {
                                /* Value struct → extrair ponteiro de string */
                                auto* msg_alloca = ctx.create_alloca(ValueTy, "msg_box");
                                B.CreateStore(msg_val, msg_alloca);
                                auto* extract_fn = ctx.ensure_runtime_func(
                                    "nv_extract_string_ptr", {ValuePtr}, I8Ptr);
                                msg_ptr = B.CreateCall(extract_fn, {msg_alloca}, "msg_cstr");
                            }
                        }
                    }
                }

                auto* create_fn = ctx.ensure_runtime_func(create_name, {ValuePtr, I8Ptr});
                B.CreateCall(create_fn, {exc_alloca, msg_ptr});
                created = true;
            }
        }
    }

    /*  Caso 2: throw expr (variável ou expressão genérica)  */
    if (!created && exception) {
        exception->codegen(ctx);
        if (ctx.has_value()) {
            auto* val = ctx.pop_value();
            if (val && val->getType() == ValueTy) {
                B.CreateStore(val, exc_alloca);
                created = true;
            } else if (val && val->getType() == ValuePtr) {
                auto* loaded = B.CreateLoad(ValueTy, val);
                B.CreateStore(loaded, exc_alloca);
                created = true;
            }
        }
    }

    /*  Fallback: criar Error genérico  */
    if (!created) {
        auto* create_fn = ctx.ensure_runtime_func("create_error", {ValuePtr, I8Ptr});
        auto* msg = B.CreateGlobalString("runtime exception", "default_exc_msg");
        B.CreateCall(create_fn, {exc_alloca, msg});
    }

    /*  Lançar a exceção  */
    auto* throw_fn = ctx.ensure_runtime_func("nv_throw_exception", {ValuePtr});
    B.CreateCall(throw_fn, {exc_alloca});
    B.CreateUnreachable();

    /* Bloco morto para absorver código após throw */
    auto* dead_bb = ctx.create_block("throw.dead");
    B.SetInsertPoint(dead_bb);
}
