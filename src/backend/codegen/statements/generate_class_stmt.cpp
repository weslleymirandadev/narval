#include "frontend/ast/statements/class_stmt_node.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <llvm/IR/Verifier.h>

// Compile one method of a class.
// Constructor (name=="new"): void __ctor_ClassName(Value* __this, Value* p0, ...)
// Other methods:             Value __method_ClassName_name(Value* __this, Value* p0, ...)
static void compile_class_method(
    nv::IRGenerationContext& ctx,
    const std::string& class_name,
    const std::string& method_name,
    FunctionStmtNode* function)
{
    auto& C  = ctx.get_context();
    auto& M  = ctx.get_module();
    auto& B  = ctx.get_builder();

    auto* ValueTy  = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
    auto* VoidTy   = llvm::Type::getVoidTy(C);

    bool is_ctor = (method_name == "new");

    std::string fn_name = is_ctor
        ? "__ctor_" + class_name
        : "__method_" + class_name + "_" + method_name;

    // All params are Value*: first is __this, rest are declared params
    std::vector<llvm::Type*>  param_types = {ValuePtr};
    std::vector<std::string>  param_names = {"__this"};
    for (const auto& pn : function->parameters) {
        for (const auto& kv : pn.parameter) {
            param_types.push_back(ValuePtr);
            param_names.push_back(kv.first);
        }
    }

    // Constructor returns void; regular methods return Value struct
    llvm::Type* ret_ty = is_ctor ? VoidTy : static_cast<llvm::Type*>(ValueTy);

    auto* fn_ty = llvm::FunctionType::get(ret_ty, param_types, false);
    auto* fn = M.getFunction(fn_name);
    if (!fn) {
        fn = llvm::Function::Create(fn_ty, llvm::Function::ExternalLinkage, fn_name, M);
    }

    // Save outer codegen state
    llvm::Function*   prev_fn = ctx.get_current_function();
    llvm::BasicBlock* prev_bb = B.GetInsertBlock();

    ctx.set_current_function(fn);
    auto* entry = llvm::BasicBlock::Create(C, "entry", fn);
    B.SetInsertPoint(entry);

    // Name LLVM arguments
    unsigned idx = 0;
    for (auto& arg : fn->args()) {
        if (idx < param_names.size()) arg.setName(param_names[idx++]);
    }

    ctx.enter_scope();

    // Register each parameter: load the Value from the Value* arg and store in an alloca
    idx = 0;
    for (auto& arg : fn->args()) {
        auto* alloca = ctx.create_and_register_variable(
            std::string(arg.getName()), ValueTy, nullptr, false);
        auto* loaded = B.CreateLoad(ValueTy, &arg);
        B.CreateStore(loaded, alloca);
        idx++;
    }

    // Push traceback frame para o método
    const std::string& src_file = ctx.get_source_file();
    std::string display_name = class_name + "." + method_name;
    if (!src_file.empty())
        nv::ir_utils::emit_push_frame(ctx, src_file, display_name);

    // Compile body statements
    for (const auto& stmt : function->body) {
        if (stmt) {
            if (stmt->position && !src_file.empty())
                nv::ir_utils::emit_set_line(ctx, static_cast<int>(stmt->position->line));
            stmt->codegen(ctx);
        }
    }

    ctx.exit_scope();

    // Terminate the last block if needed
    auto* cur_bb = B.GetInsertBlock();
    if (cur_bb && !cur_bb->getTerminator()) {
        if (!src_file.empty()) nv::ir_utils::emit_pop_frame(ctx);
        if (is_ctor || ret_ty->isVoidTy()) {
            B.CreateRetVoid();
        } else {
            if (ctx.has_value()) {
                auto* ret_val = ctx.pop_value();
                if (ret_val && ret_val->getType() == ValueTy) {
                    B.CreateRet(ret_val);
                } else {
                    B.CreateRet(llvm::UndefValue::get(ValueTy));
                }
            } else {
                B.CreateRet(llvm::UndefValue::get(ValueTy));
            }
        }
    }

    // Restore outer codegen state
    ctx.set_current_function(prev_fn);
    if (prev_bb) B.SetInsertPoint(prev_bb);
}

void ClassStmtNode::codegen(nv::IRGenerationContext& ctx) {
    for (const auto& method : methods) {
        if (!method->method_def) continue;
        auto* function = static_cast<FunctionStmtNode*>(method->method_def.get());
        compile_class_method(ctx, name, method->name, function);
    }
}
