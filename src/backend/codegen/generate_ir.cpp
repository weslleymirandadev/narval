#include "backend/codegen/generate_ir.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/ast/statements/module_attr_node.hpp"
#include "frontend/ast/statements/attribute_stmt_node.hpp"
#include "frontend/ast/statements/decorator_stmt_node.hpp"
#include <unordered_set>
#include <string>
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
    else if (feature == "str") g_feature_tracker.has_strings = true;
    else if (feature == "map") g_feature_tracker.has_maps = true;
    else if (feature == "read") g_feature_tracker.has_read = true;
    else if (feature == "write") g_feature_tracker.has_write = true;
    else if (feature == "map_operations") g_feature_tracker.has_map_operations = true;
    else if (feature == "string_operations") g_feature_tracker.has_string_operations = true;
    else if (feature == "vector_operations") g_feature_tracker.has_vector_operations = true;
    else if (feature == "closures") g_feature_tracker.has_closures = true;
}

// Função para obter o tracker atual
const FeatureTracker& get_feature_tracker() {
    return g_feature_tracker;
}

FeatureTracker& get_mutable_feature_tracker() {
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
    auto* I8Ptr   = llvm::PointerType::getUnqual(C);

    // Object and Array opaque pointers
    auto* ObjPtr  = I8Ptr;
    auto* ArrPtr  = I8Ptr;

    const auto& tracker = get_feature_tracker();
    auto* F64 = llvm::Type::getDoubleTy(C);

    // ── Núcleo sintático (sempre declarado) ──────────────────────────────────
    // Criação de literais
    M.getOrInsertFunction("create_int",   llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false));
    M.getOrInsertFunction("create_float", llvm::FunctionType::get(VoidTy, {ValuePtr, F64}, false));
    M.getOrInsertFunction("create_bool",  llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false));
    M.getOrInsertFunction("create_str",   llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr}, false));
    // Aritmética e comparação polimórfica
    M.getOrInsertFunction("nv_value_add", llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, ValuePtr}, false));
    M.getOrInsertFunction("nv_value_sub", llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, ValuePtr}, false));
    M.getOrInsertFunction("nv_value_mul", llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, ValuePtr}, false));
    M.getOrInsertFunction("nv_value_div", llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, ValuePtr}, false));
    M.getOrInsertFunction("nv_value_mod", llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, ValuePtr}, false));
    M.getOrInsertFunction("nv_value_cmp", llvm::FunctionType::get(I32,    {ValuePtr, ValuePtr}, false));
    // Extração de primitivos (necessário para condicionais e loops)
    M.getOrInsertFunction("extract_int_from_value",    llvm::FunctionType::get(I32,   {ValuePtr}, false));
    M.getOrInsertFunction("extract_float_from_value",  llvm::FunctionType::get(F64,   {ValuePtr}, false));
    M.getOrInsertFunction("extract_string_from_value", llvm::FunctionType::get(I8Ptr, {ValuePtr}, false));
    M.getOrInsertFunction("nv_extract_string_ptr",     llvm::FunctionType::get(I8Ptr, {ValuePtr}, false));
    // memset usado internamente pelo codegen
    M.getOrInsertFunction("memset", llvm::FunctionType::get(I8Ptr, {I8Ptr, I32, I64}, false));

    // Com [no_std], apenas o núcleo acima entra no binário; tudo mais é cortado.
    if (tracker.no_std) return;

    // ── Stdlib (disponível por padrão) ───────────────────────────────────────
    // Coleções
    M.getOrInsertFunction("create_map",    llvm::FunctionType::get(VoidTy, {ValuePtr}, false));
    M.getOrInsertFunction("create_array",  llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false));
    M.getOrInsertFunction("create_vector", llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false));
    M.getOrInsertFunction("create_tuple",  llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false));
    // Option / Result
    M.getOrInsertFunction("create_option_some", llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr}, false));
    M.getOrInsertFunction("create_option_none", llvm::FunctionType::get(VoidTy, {ValuePtr}, false));
    M.getOrInsertFunction("create_result_ok",   llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr}, false));
    M.getOrInsertFunction("create_result_err",  llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr}, false));
    M.getOrInsertFunction("nv_is_failure",      llvm::FunctionType::get(I32,    {ValuePtr}, false));
    M.getOrInsertFunction("nv_unwrap_inner",    llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr}, false));
    M.getOrInsertFunction("nv_get_failure_err", llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr}, false));
    // Conversões de tipo (str(), int(), float(), bool())
    M.getOrInsertFunction("nv_str_convert",   llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr}, false));
    M.getOrInsertFunction("nv_int_convert",   llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr}, false));
    M.getOrInsertFunction("nv_float_convert", llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr}, false));
    M.getOrInsertFunction("nv_bool_convert",  llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr}, false));
    M.getOrInsertFunction("int_to_string",    llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false));
    M.getOrInsertFunction("float_to_string",  llvm::FunctionType::get(VoidTy, {ValuePtr, F64}, false));
    // I/O
    M.getOrInsertFunction("nv_write",       llvm::FunctionType::get(VoidTy, {ValuePtr}, false));
    M.getOrInsertFunction("nv_write_no_nl", llvm::FunctionType::get(VoidTy, {ValuePtr}, false));
    M.getOrInsertFunction("nv_read",        llvm::FunctionType::get(I8Ptr, {I8Ptr}, false));
    M.getOrInsertFunction("atoi",           llvm::FunctionType::get(I32,   {I8Ptr}, false));
    M.getOrInsertFunction("_exit",          llvm::FunctionType::get(VoidTy, {I32},  false));
    // Traceback / shadow call stack
    M.getOrInsertFunction("nv_push_frame", llvm::FunctionType::get(VoidTy, {I8Ptr, I8Ptr}, false));
    M.getOrInsertFunction("nv_pop_frame",  llvm::FunctionType::get(VoidTy, {}, false));
    M.getOrInsertFunction("nv_set_line",   llvm::FunctionType::get(VoidTy, {I32}, false));
    // Exception handling (setjmp-based)
    M.getOrInsertFunction("nv_push_try_handler",           llvm::FunctionType::get(I8Ptr, {}, false));
    M.getOrInsertFunction("nv_pop_try_handler",            llvm::FunctionType::get(VoidTy, {}, false));
    M.getOrInsertFunction("nv_get_current_exception_into", llvm::FunctionType::get(VoidTy, {ValuePtr}, false));
    M.getOrInsertFunction("nv_get_exception_message_into", llvm::FunctionType::get(VoidTy, {ValuePtr}, false));
    M.getOrInsertFunction("nv_exception_matches",          llvm::FunctionType::get(I32,    {I8Ptr}, false));
    M.getOrInsertFunction("nv_clear_current_exception",    llvm::FunctionType::get(VoidTy, {}, false));
    M.getOrInsertFunction("nv_rethrow_current_exception",  llvm::FunctionType::get(VoidTy, {}, false));
    M.getOrInsertFunction("nv_throw_exception",            llvm::FunctionType::get(VoidTy, {ValuePtr}, false));
    M.getOrInsertFunction("nv_create_exception",           llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr, I8Ptr}, false));
    M.getOrInsertFunction("create_error",            llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr}, false));
    M.getOrInsertFunction("create_value_error",      llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr}, false));
    M.getOrInsertFunction("create_type_error",       llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr}, false));
    M.getOrInsertFunction("create_runtime_error",    llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr}, false));
    M.getOrInsertFunction("create_index_error",      llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr}, false));
    M.getOrInsertFunction("create_key_error",        llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr}, false));
    M.getOrInsertFunction("create_attribute_error",  llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr}, false));
    M.getOrInsertFunction("create_name_error",       llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr}, false));
    M.getOrInsertFunction("create_assertion_error",  llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr}, false));
    // Métodos de coleções (condicionais por feature tracker)
    if (tracker.has_maps || tracker.has_map_operations) {
        M.getOrInsertFunction("map_get_method", llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, I8Ptr}, false));
        M.getOrInsertFunction("map_set_method", llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr, ValuePtr}, false));
    }
    if (tracker.has_strings || tracker.has_string_operations) {
        M.getOrInsertFunction("string_to_upper_case", llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr}, false));
        M.getOrInsertFunction("string_replace",       llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr, I8Ptr, I8Ptr}, false));
        M.getOrInsertFunction("string_includes",      llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr, ValueTy}, false));
    }
    if (tracker.has_vectors || tracker.has_vector_operations) {
        M.getOrInsertFunction("vector_get_method", llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, I32}, false));
        M.getOrInsertFunction("vector_set_method", llvm::FunctionType::get(VoidTy, {ValuePtr, I32, ValuePtr}, false));
    }
    
    // REPL helpers
    M.getOrInsertFunction("nv_register_write_value",    llvm::FunctionType::get(VoidTy, {I8Ptr}, false));
    M.getOrInsertFunction("nv_register_function_return",llvm::FunctionType::get(VoidTy, {I8Ptr, I8Ptr}, false));
    M.getOrInsertFunction("ensure_value_type",          llvm::FunctionType::get(VoidTy, {ValuePtr}, false));
    M.getOrInsertFunction("nv_get_iterable_length",     llvm::FunctionType::get(I32,    {ValuePtr}, false));
    M.getOrInsertFunction("array_get_index_v",          llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, I32}, false));
    M.getOrInsertFunction("nv_set_at_index",            llvm::FunctionType::get(VoidTy, {ValuePtr, I32, ValuePtr}, false));
    M.getOrInsertFunction("nv_collection_slice",        llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, I32, I32, I32}, false));

    // Closures (declaradas se a feature for usada)
    if (tracker.has_closures) {
        M.getOrInsertFunction("create_closure", llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr}, false));
        M.getOrInsertFunction("create_closure_with_captures", llvm::FunctionType::get(VoidTy, {ValuePtr, I8Ptr, ValuePtr, I32}, false));
        M.getOrInsertFunction("call_closure", llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, I32, ValuePtr}, false));
    }
}

void generate_ir(
    std::unique_ptr<Node> node,
    IRGenerationContext& context,
    bool keep_result
) {
    // Safely handle Node that may be a Program or a single statement.
    Node* raw = node.release();
    Program* p = (raw && raw->kind == NodeType::Program) ? static_cast<Program*>(raw) : nullptr;
    std::unique_ptr<Program> program;
    if (p) {
        // take ownership of the Program
        program.reset(p);
    } else {
        // wrap a single statement into a Program to avoid null deref
        program = std::make_unique<Program>();
        if (raw) {
            // All non-Program nodes are Stmt (or subclass)
            program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(raw)));
        }
    }

    // Pré-scan: aplicar atributos de módulo ANTES de declare_runtime
    // para que as flags de desabilitação já estejam definidas.
    for (const auto& stmt : program->body) {
        if (stmt && stmt->kind == NodeType::ModuleAttrStatement) {
            auto* attr = static_cast<ModuleAttrNode*>(stmt.get());
            for (const auto& a : attr->attrs)
                g_feature_tracker.apply_attribute(a);
        } else if (stmt && stmt->kind == NodeType::AttributeStatement) {
            auto* attr = static_cast<AttributeStmtNode*>(stmt.get());
            for (const auto& a : attr->attrs)
                g_feature_tracker.apply_attribute(a);
        } else if (stmt && stmt->kind == NodeType::DecoratorStatement) {
            auto* decor = static_cast<DecoratorStmtNode*>(stmt.get());
            for (const auto& d : decor->decorators)
                g_feature_tracker.apply_decorator(d);
        }
    }

    // Ensure runtime declarations are present in the module
    declare_runtime(context);

    context.enter_scope();

    // Passagem 1: Declarar todas as funções (para suportar referências futuras e recursão mútua)
    for (size_t i = 0; i < program->body.size(); ++i) {
        if (program->body[i]->kind == NodeType::FunctionStatement) {
            auto* def_stmt = static_cast<FunctionStmtNode*>(program->body[i].get());
            auto* checker = static_cast<nv::Checker*>(context.get_type_checker());
            
            std::vector<llvm::Type*> param_types;
            for (auto& p : def_stmt->parameters) {
                for (auto& kv : p.parameter) {
                    llvm::Type* param_ty = nullptr;
                    if (checker) {
                        try {
                            auto param_nv_ty = checker->gettyptr(kv.second);
                            param_ty = context.nv_type_to_llvm(param_nv_ty);
                        } catch (...) {
                            param_ty = nullptr;
                        }
                    }
                    if (!param_ty) {
                        param_ty = nv::ir_utils::llvm_type_from_string(context, kv.second);
                    }
                    // Tipos compostos Narval: sempre usar Value struct (evitar mismatch com ptr)
                    {
                        static const std::unordered_set<std::string> composite = {
                            "vector", "array", "map", "tuple"
                        };
                        if (composite.count(kv.second) ||
                            !param_ty || param_ty->isVoidTy() ||
                            (param_ty->isPointerTy() && kv.second != "str" && kv.second != "void")) {
                            param_ty = nv::ir_utils::get_value_struct(context);
                        }
                    }
                    param_types.push_back(param_ty);
                }
            }
            llvm::Type* ret_ty = nullptr;
            if (checker) {
                try {
                    auto ret_nv_ty = checker->gettyptr(def_stmt->return_type);
                    ret_ty = context.nv_type_to_llvm(ret_nv_ty);
                } catch (...) {
                    ret_ty = nullptr;
                }
            }
            if (!ret_ty) {
                ret_ty = nv::ir_utils::llvm_type_from_string(context, def_stmt->return_type);
            }
            if (!ret_ty) {
                ret_ty = llvm::Type::getVoidTy(context.get_context());
            }
            // Funções falíveis retornam Value (Result::Ok ou Result::Err)
            if (def_stmt->is_fallible) {
                ret_ty = nv::ir_utils::get_value_struct(context);
            }
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

    // Push frame do módulo no shadow stack de traceback
    const std::string& src_file = context.get_source_file();
    if (!src_file.empty()) {
        ir_utils::emit_push_frame(context, src_file, "<module>");
    }

    for (size_t i = 0; i < program->body.size(); ++i) {
        auto* stmt = program->body[i].get();
        if (stmt && stmt->position && !src_file.empty())
            ir_utils::emit_set_line(context, static_cast<int>(stmt->position->line));
        stmt->codegen(context);
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