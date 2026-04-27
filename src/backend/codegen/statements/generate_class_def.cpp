#include "frontend/ast/statements/class_def_node.hpp"
#include "frontend/ast/statements/def_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <llvm/IR/Verifier.h>

void ClassDefNode::codegen(nv::IRGenerationContext& ctx) {
    auto& C  = ctx.get_context();
    auto& M  = ctx.get_module();
    auto& B  = ctx.get_builder();

    auto* ValueTy  = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
    auto* VoidTy   = llvm::Type::getVoidTy(C);

    for (const auto& method : methods) {
        if (method->name != "new" || !method->method_def) continue;

        auto* def = static_cast<DefStmtNode*>(method->method_def.get());

        // Signature: void __ctor_ClassName(Value* __this, Value* p0, Value* p1, ...)
        std::string fn_name = "__ctor_" + name;
        std::vector<llvm::Type*> param_types = {ValuePtr}; // __this
        std::vector<std::string> param_names = {"__this"};
        for (const auto& pn : def->parameters) {
            for (const auto& kv : pn.parameter) {
                param_types.push_back(ValuePtr);
                param_names.push_back(kv.first);
            }
        }

        auto* fn_ty = llvm::FunctionType::get(VoidTy, param_types, false);
        auto* fn = M.getFunction(fn_name);
        if (!fn) {
            fn = llvm::Function::Create(fn_ty, llvm::Function::ExternalLinkage, fn_name, M);
        }

        // Save state
        llvm::Function*    prev_fn    = ctx.get_current_function();
        llvm::BasicBlock*  prev_bb    = B.GetInsertBlock();

        ctx.set_current_function(fn);
        auto* entry = llvm::BasicBlock::Create(C, "entry", fn);
        B.SetInsertPoint(entry);

        // Name args
        unsigned idx = 0;
        for (auto& arg : fn->args()) {
            if (idx < param_names.size()) arg.setName(param_names[idx++]);
        }

        ctx.enter_scope();

        // Register parameters in symbol table as Value allocas
        idx = 0;
        for (auto& arg : fn->args()) {
            auto* alloca = ctx.create_and_register_variable(
                std::string(arg.getName()), ValueTy, nullptr, false);
            // Load the Value from the Value* parameter and store in alloca
            auto* loaded = B.CreateLoad(ValueTy, &arg);
            B.CreateStore(loaded, alloca);
            idx++;
        }

        // Compile constructor body
        for (const auto& stmt : def->body) {
            if (stmt) stmt->codegen(ctx);
        }

        ctx.exit_scope();

        if (!B.GetInsertBlock()->getTerminator()) {
            B.CreateRetVoid();
        }

        // Restore state
        ctx.set_current_function(prev_fn);
        if (prev_bb) B.SetInsertPoint(prev_bb);
    }
}
