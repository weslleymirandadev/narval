#pragma once
#include "frontend/ast/statements/extern_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include <string_view>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  Tabela de tipos FFI
//
//  Centraliza todas as conversões Narval ↔ nativo em uma única struct.
//  Adicionar suporte a um novo tipo = adicionar uma entrada em FFI_TYPE_TABLE.
// ─────────────────────────────────────────────────────────────────────────────

namespace nv::ffi {

struct TypeDesc {
    std::string_view narval;      // "str", "int", "float", "bool", "void"

    // LLVM wrapper (C-ABI backend) ─────────────────────────────────────────
    std::string_view extract_fn;  // Value* → nativo:  nv_extract_string_ptr, extract_int_from_value
    std::string_view box_fn;      // nativo → Value*:  create_str, create_int, create_option_none

    // Bridge C genérico ───────────────────────────────────────────────────
    std::string_view c_type;      // "const char*", "int32_t", "double"

    // Python C API  (formato: %s = expressão do valor nativo / PyObject*)
    std::string_view py_from;     // nativo → PyObject*:  "PyUnicode_FromString(%s)"
    std::string_view py_to;       // PyObject* → nativo:  "PyUnicode_AsUTF8(%s)"
    std::string_view py_box;      // PyObject* → NvValue:  "create_str(&_nv_r, %s)"

    // Slot para extensões futuras (Lua, Ruby, etc.)
    // std::string_view lua_push;
    // std::string_view lua_to;
};

// Retorna o descriptor para um tipo Narval; fallback = "str" para tipos desconhecidos.
const TypeDesc& type_desc(std::string_view narval_type);

// Retorna o tipo LLVM nativo correspondente a um tipo Narval.
llvm::Type* llvm_native_type(std::string_view narval_type, llvm::LLVMContext& C);

// ─────────────────────────────────────────────────────────────────────────────
//  Interface de backend FFI
//
//  Cada família de linguagem implementa esta interface.
//  Adicionar suporte a uma nova linguagem = criar uma subclasse e registrá-la.
// ─────────────────────────────────────────────────────────────────────────────

class Backend {
public:
    virtual ~Backend() = default;

    // Emite as declarações/wrappers no módulo LLVM e agenda itens de link.
    virtual void emit(const ExternStmtNode& node, IRGenerationContext& ctx) = 0;
};

// Factory: retorna o backend correto para a linguagem/source_file dados.
//   extern "C"       { ... }                   → CABIBackend
//   extern "Python"  { ... }  (sem from)       → CABIBackend  (usuário traz bridge)
//   extern "Python" from "x.py" { ... }        → PythonBackend (bridge automático)
//
// Para adicionar uma nova linguagem: crie um backend, registre aqui.
Backend& backend_for(const std::string& language, const std::string& source_file);

// ─────────────────────────────────────────────────────────────────────────────
//  API para `from extern "lang:lib" import items`
// ─────────────────────────────────────────────────────────────────────────────

struct CLibFunc {
    std::string name;
    std::string return_type;
    std::vector<std::pair<std::string,std::string>> params; // (nome, tipo_narval)
};

// Registry de assinaturas de bibliotecas C padrão (math, string, stdlib, stdio, ...).
std::vector<CLibFunc> c_lib_registry(const std::string& libname);

// Emite declaração + wrapper LLVM para uma função C do registry.
void emit_c_func(const CLibFunc& fn, IRGenerationContext& ctx);

// Emite bridge Python para namespace (`from extern "Python:mod" import * as alias`).
// Gera em C: _nv_py_ns_ALIAS + _nv_py_ns_call_ALIAS(method, args[], n) → Value.
// Declara o dispatcher no módulo LLVM e registra `alias` como py_namespace no ctx.
void emit_python_namespace(const std::string& module, const std::string& alias,
                            IRGenerationContext& ctx);

// Emite wrappers para funções específicas de módulo Python
// (`from extern "Python:numpy" import array, zeros`).
void emit_python_module_funcs(
    const std::string& module,
    const std::vector<std::pair<std::string,std::string>>& funcs, // (name, alias)
    IRGenerationContext& ctx);

} // namespace nv::ffi
