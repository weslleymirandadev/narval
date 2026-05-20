#include "frontend/ast/statements/inline_asm_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <llvm/IR/InlineAsm.h>
#include <string>
#include <map>

// Símbolo âncora: referenciado de generate_ir.cpp para forçar este objeto
// a ser incluído pelo linker (necessário para que o strong codegen supere o weak stub do checker)
namespace nv { void _register_inline_asm_codegen_module() {} }

void InlineAsmStmtNode::codegen(nv::IRGenerationContext& ctx) {
    auto& builder  = ctx.get_builder();
    auto& llvm_ctx = ctx.get_context();
    auto& sym_tab  = ctx.get_symbol_table();

    // --- 1. Mapear nome → índice de operando do template LLVM ---
    // LLVM numera: outputs ($0..N_out-1), depois inputs ($N_out..N_out+N_in-1)
    // Inputs tied ao output usam o índice do output correspondente.
    // Inputs não-tied recebem N_out + posição_no_array_de_inputs.
    std::map<std::string, int> name_to_idx;
    int out_count = (int)outputs.size();
    for (int i = 0; i < out_count; i++)
        name_to_idx[outputs[i].from_var] = i;
    for (int i = 0; i < (int)inputs.size(); i++) {
        if (name_to_idx.find(inputs[i].var_name) == name_to_idx.end())
            name_to_idx[inputs[i].var_name] = out_count + i;
    }

    // --- 2. Construir constraint string ---
    std::string cstr;
    for (size_t i = 0; i < outputs.size(); i++) {
        if (!cstr.empty()) cstr += ",";
        cstr += "=r";
    }
    for (auto& in : inputs) {
        if (!cstr.empty()) cstr += ",";
        bool tied = false;
        for (size_t i = 0; i < outputs.size(); i++) {
            if (outputs[i].from_var == in.var_name) {
                cstr += std::to_string(i);
                tied = true;
                break;
            }
        }
        if (!tied) cstr += "r";
    }

    // --- 3. Converter template {nome} → $N ---
    std::string tmpl = asm_template;
    for (auto& [name, n] : name_to_idx) {
        std::string ph  = "{" + name + "}";
        std::string rep = "$" + std::to_string(n);
        size_t pos = 0;
        while ((pos = tmpl.find(ph, pos)) != std::string::npos) {
            tmpl.replace(pos, ph.size(), rep);
            pos += rep.size();
        }
    }

    // --- 4. Carregar valores dos inputs ---
    std::vector<llvm::Type*>  in_types;
    std::vector<llvm::Value*> in_vals;
    for (auto& in : inputs) {
        auto sym = sym_tab.lookup_symbol(in.var_name);
        if (!sym) continue;
        llvm::Value* val = sym->value;
        llvm::Type*  ty  = sym->llvm_type;
        if (sym->is_allocated && ty) {
            val = builder.CreateLoad(ty, val, in.var_name + ".ll");
        } else if (val->getType()->isPointerTy() && ty) {
            val = builder.CreateLoad(ty, val, in.var_name + ".ll");
        }
        in_types.push_back(val->getType());
        in_vals.push_back(val);
    }

    // --- 5. Determinar tipos de output ---
    std::vector<llvm::Type*> out_types;
    for (auto& out : outputs) {
        llvm::Type* ty = llvm::Type::getInt64Ty(llvm_ctx); // default
        for (size_t i = 0; i < inputs.size(); i++) {
            if (inputs[i].var_name == out.from_var && i < in_types.size()) {
                ty = in_types[i];
                break;
            }
        }
        out_types.push_back(ty);
    }

    llvm::Type* ret_type = nullptr;
    if (out_types.empty()) {
        ret_type = llvm::Type::getVoidTy(llvm_ctx);
    } else if (out_types.size() == 1) {
        ret_type = out_types[0];
    } else {
        ret_type = llvm::StructType::get(llvm_ctx, out_types);
    }

    // --- 6. Emitir InlineAsm ---
    auto* fn_type  = llvm::FunctionType::get(ret_type, in_types, false);
    auto* asm_inst = llvm::InlineAsm::get(
        fn_type, tmpl, cstr,
        /* hasSideEffects = */ true,
        /* isAlignStack   = */ false,
        llvm::InlineAsm::AD_ATT
    );
    llvm::Value* result = builder.CreateCall(fn_type, asm_inst, in_vals, "asm.out");

    // --- 7. Vincular outputs ao escopo ---
    for (size_t i = 0; i < outputs.size(); i++) {
        llvm::Value* out_val = out_types.size() == 1
            ? result
            : builder.CreateExtractValue(result, {(unsigned)i});

        auto* alloca = ctx.create_alloca(out_types[i], outputs[i].result_var);
        builder.CreateStore(out_val, alloca);

        nv::SymbolInfo info(alloca, out_types[i], nullptr, true, false);
        sym_tab.define_symbol(outputs[i].result_var, info);
    }
}
