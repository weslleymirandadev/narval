#include "backend/codegen/generate_ir.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_ostream.h"

namespace nv {

// Tracker global - será atualizado pelos arquivos de generate
static FeatureTracker g_feature_tracker;

// Função para registrar uso de features
void register_feature(const std::string& feature) {
    if (feature == "vector") g_feature_tracker.has_vectors = true;
    else if (feature == "string") g_feature_tracker.has_strings = true;
    else if (feature == "map") g_feature_tracker.has_maps = true;
    else if (feature == "json") g_feature_tracker.has_json = true;
    else if (feature == "read") g_feature_tracker.has_read = true;
    else if (feature == "write") g_feature_tracker.has_write = true;
    else if (feature == "map_operations") g_feature_tracker.has_map_operations = true;
    else if (feature == "string_operations") g_feature_tracker.has_string_operations = true;
    else if (feature == "vector_operations") g_feature_tracker.has_vector_operations = true;
}

// Função para obter o tracker atual
const FeatureTracker& get_feature_tracker() {
    return g_feature_tracker;
}

static void declare_runtime(IRGenerationContext& context) {
    auto& M = context.get_module();
    auto& C = context.get_context();
    // Use the canonical Value type from ir_utils so all codegen agrees on the same struct
    auto* ValueTy  = ir_utils::get_value_struct(context);
    auto* ValuePtr = ir_utils::get_value_ptr(context);
    auto* VoidTy  = llvm::Type::getVoidTy(C);
    auto* I32     = llvm::Type::getInt32Ty(C);
    auto* I64     = llvm::Type::getInt64Ty(C);
    auto* I8      = llvm::Type::getInt8Ty(C);
    auto* I8Ptr   = llvm::PointerType::getUnqual(I8);

    // Object and Array opaque pointers
    auto* ObjPtr  = I8Ptr;
    auto* ArrPtr  = I8Ptr;

    // Prototypes for runtime functions (subset sufficient for stdlib usage)
    // Value helpers (plain Value* out parameters, no sret)
    {
        auto decl = M.getOrInsertFunction("create_str", llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr}, false));
    }
    {
        auto decl = M.getOrInsertFunction("create_float", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::Type::getDoubleTy(C)}, false));
    }
    {
        auto decl = M.getOrInsertFunction("create_int", llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false));
    }
    {
        auto decl = M.getOrInsertFunction("create_bool", llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false));
    }
    {
        auto decl = M.getOrInsertFunction("create_map", llvm::FunctionType::get(VoidTy, {ValuePtr}, false));
    }
    {
        auto decl = M.getOrInsertFunction("create_array", llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false));
    }
    {
        auto decl = M.getOrInsertFunction("create_vector", llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false));
    }
    
    // Funções de conversão de tipo (estilo Python)
    // Convenção: void fn(Value* out, Value* in)
    {
        auto decl = M.getOrInsertFunction("nv_str_convert", llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr}, false));
    }
    {
        auto decl = M.getOrInsertFunction("nv_int_convert", llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr}, false));
    }
    {
        auto decl = M.getOrInsertFunction("nv_float_convert", llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr}, false));
    }
    {
        auto decl = M.getOrInsertFunction("nv_bool_convert", llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr}, false));
    }

    // nv_write(Value*) - função builtin para escrita com nova linha
    M.getOrInsertFunction("nv_write", llvm::FunctionType::get(VoidTy, {ValuePtr}, false));
    // nv_write_no_nl(Value*) - função builtin para escrita sem nova linha
    M.getOrInsertFunction("nv_write_no_nl", llvm::FunctionType::get(VoidTy, {ValuePtr}, false));

    M.getOrInsertFunction("nv_read", llvm::FunctionType::get(I8Ptr, {}, false));
    M.getOrInsertFunction("atoi", llvm::FunctionType::get(I32, {I8Ptr}, false));
    M.getOrInsertFunction("_exit", llvm::FunctionType::get(VoidTy, {I32}, false));

    // Funções condicionais baseadas no tracker
    const auto& tracker = get_feature_tracker();
    
    // JSON functions
    if (tracker.has_json) {
        M.getOrInsertFunction("json_parse", llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr}, false));
        M.getOrInsertFunction("json_parse_string", llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr}, false));
    }
    
    // Map functions
    if (tracker.has_maps || tracker.has_map_operations) {
        M.getOrInsertFunction("map_get_method", llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, I8Ptr}, false));
        M.getOrInsertFunction("map_set_method", llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr, ValuePtr}, false));
    }
    
    // String functions
    if (tracker.has_strings || tracker.has_string_operations) {
        auto decl = M.getOrInsertFunction("string_to_upper_case", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::PointerType::getUnqual(ValueTy)}, false));
    }
    
    if (tracker.has_strings || tracker.has_string_operations) {
        auto decl = M.getOrInsertFunction("string_replace", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::PointerType::getUnqual(ValueTy), llvm::PointerType::getUnqual(ValueTy), llvm::PointerType::getUnqual(ValueTy)}, false));
    }
    
    if (tracker.has_strings || tracker.has_string_operations) {
        auto decl = M.getOrInsertFunction("string_includes", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::PointerType::getUnqual(ValueTy), ValueTy}, false));
    }
    
    // Vector functions
    if (tracker.has_vectors || tracker.has_vector_operations) {
        M.getOrInsertFunction(
            "vector_get_method",
            llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, I32}, false)
        );
    }
    
    if (tracker.has_vectors || tracker.has_vector_operations) {
        M.getOrInsertFunction(
            "vector_set_method",
            llvm::FunctionType::get(VoidTy, {ValuePtr, I32, ValuePtr}, false)
        );
    }
    
    // REPL helper functions (sempre necessárias para modo interativo)
    M.getOrInsertFunction("nv_register_write_value", llvm::FunctionType::get(VoidTy, {llvm::PointerType::getUnqual(ValueTy)}, false));
    M.getOrInsertFunction("nv_register_function_return", llvm::FunctionType::get(VoidTy, {llvm::PointerType::getUnqual(ValueTy), I8Ptr}, false));
    M.getOrInsertFunction("ensure_value_type", llvm::FunctionType::get(VoidTy, {ValuePtr}, false));
    
    // Plain C helper usado por codegen para string repetition
    M.getOrInsertFunction("memset", llvm::FunctionType::get(I8Ptr, {I8Ptr, I32, I64}, false));
}

void generate_ir(
    std::unique_ptr<Node> node,
    IRGenerationContext& context,
    bool keep_result
) {
    // Safely handle Node that may be a Program or a single statement.
    Node* raw = node.release();
    Program* p = dynamic_cast<Program*>(raw);
    std::unique_ptr<Program> program;
    if (p) {
        // take ownership of the Program
        program.reset(p);
    } else {
        // wrap a single statement into a Program to avoid null deref
        program = std::make_unique<Program>();
        if (raw) {
            if (auto* stmt = dynamic_cast<Stmt*>(raw)) {
                program->add_statement(std::unique_ptr<Stmt>(stmt));
            } else {
                // unknown node type: delete to avoid leak
                delete raw;
            }
        }
    }

    // Ensure runtime declarations are present in the module
    declare_runtime(context);

    context.enter_scope();

    // Passagem 1: Declarar todas as funções (para suportar referências futuras e recursão mútua)
    for (size_t i = 0; i < program->body.size(); ++i) {
        if (program->body[i]->kind == NodeType::DefStatement) {
            auto* def_stmt = static_cast<DefStmtNode*>(program->body[i].get());
            
            std::vector<llvm::Type*> param_types;
            for (auto& p : def_stmt->parameters) {
                for (auto& kv : p.parameter) {
                    param_types.push_back(nv::ir_utils::llvm_type_from_string(context, kv.second));
                }
            }
            llvm::Type* ret_ty = nv::ir_utils::llvm_type_from_string(context, def_stmt->return_type);
            auto* fn_ty = llvm::FunctionType::get(ret_ty, param_types, false);
            
            // Criar a declaração da função no módulo LLVM se ainda não existir
            auto* fn = context.get_module().getFunction(def_stmt->name);
            if (!fn) {
                fn = llvm::Function::Create(fn_ty, llvm::Function::ExternalLinkage, def_stmt->name, context.get_module());
            }
            
            // Registrar o símbolo da função na tabela de símbolos do contexto
            nv::SymbolInfo fn_info(fn, fn->getType(), nullptr, false, true);
            context.get_symbol_table().define_symbol(def_stmt->name, fn_info);
        }
    }

    // register_global_init é chamado uma única vez em main.cpp antes de generate_ir()
    // Não duplicar o call aqui para evitar realocação dos type pointers

    for (size_t i = 0; i < program->body.size(); ++i) {
        program->body[i]->codegen(context);
    }

    // In REPL (keep_result=true) leave the last value on the stack for auto-print.
    // In batch: discard any values left (e.g. from write()), then push 0 as exit value.
    if (!keep_result) {
        while (context.has_value()) {
            (void) context.pop_value();
        }
        // Para a nova estrutura, simplesmente criar um valor int 0
        auto* ValueTy = ir_utils::get_value_struct(context);
        auto* ValuePtr = ir_utils::get_value_ptr(context);
        auto* I32 = llvm::Type::getInt32Ty(context.get_context());
        auto* zero_alloca = context.create_alloca(ValueTy, "program_exit_val");
        auto* create_int_fn = context.ensure_runtime_func("create_int", {ValuePtr, I32});
        context.get_builder().CreateCall(create_int_fn, {zero_alloca, llvm::ConstantInt::get(I32, 0)});
        context.push_value(context.get_builder().CreateLoad(ValueTy, zero_alloca, "program_exit_zero"));
    }

    context.exit_scope();
}

} // namespace nv