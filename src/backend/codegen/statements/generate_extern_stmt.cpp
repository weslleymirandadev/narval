#include "frontend/ast/statements/extern_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/checker/checker.hpp"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/IRBuilder.h"
#include <fstream>
#include <cstdlib>
#include <filesystem>

using namespace llvm;

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers de mapeamento de tipo
// ─────────────────────────────────────────────────────────────────────────────

static llvm::Type* native_type(const std::string& type_str, nv::IRGenerationContext& ctx) {
    auto& C = ctx.get_context();
    if (type_str == "str")   return PointerType::getUnqual(C);
    if (type_str == "int")   return Type::getInt32Ty(C);
    if (type_str == "float") return Type::getDoubleTy(C);
    if (type_str == "bool")  return Type::getInt32Ty(C);
    if (type_str == "void")  return Type::getVoidTy(C);
    return PointerType::getUnqual(C); // opaque ptr para tipos desconhecidos
}

// ─────────────────────────────────────────────────────────────────────────────
//  Gerador de bridge C para `extern "Python" from "arquivo.py"`
// ─────────────────────────────────────────────────────────────────────────────

// Gera o código C do bridge que:
//  1. Inicializa Python uma vez (lazy)
//  2. Carrega o arquivo .py do usuário
//  3. Para cada função declarada, gera um wrapper NvValue __extern_X(NvValue...)
//     que converte Narval Values ↔ PyObject* e chama a função Python real
static std::string generate_python_bridge_c(const ExternStmtNode& node) {
    std::string s;

    s += "#define PY_SSIZE_T_CLEAN\n";
    s += "#include <Python.h>\n";
    s += "#include <string.h>\n";
    s += "#include <stdio.h>\n\n";

    // Opaque Narval Value (compatible com o struct real { void* obj })
    s += "typedef struct { void* obj; } NvValue;\n\n";

    // Funções do runtime Narval usadas aqui
    s += "extern void create_str(NvValue* out, const char* s);\n";
    s += "extern void create_int(NvValue* out, int v);\n";
    s += "extern void create_float(NvValue* out, double v);\n";
    s += "extern void create_bool(NvValue* out, int v);\n";
    s += "extern void create_option_none(NvValue* out);\n";
    s += "extern const char* nv_extract_string_ptr(NvValue* v);\n";
    s += "extern int    extract_int_from_value(NvValue* v);\n";
    s += "extern double extract_float_from_value(NvValue* v);\n\n";

    // Estado global do bridge
    s += "static PyObject* _nv_py_globals = NULL;\n\n";

    // Inicializador lazy — carrega o .py do usuário uma única vez
    s += "static void _nv_py_ensure_init(void) {\n";
    s += "    if (_nv_py_globals) return;\n";
    s += "    Py_Initialize();\n";
    // Escapar o path para C string
    std::string escaped_path;
    for (char c : node.source_file) {
        if (c == '\\') escaped_path += "\\\\";
        else if (c == '"') escaped_path += "\\\"";
        else escaped_path += c;
    }
    s += "    FILE* _nv_f = fopen(\"" + escaped_path + "\", \"r\");\n";
    s += "    if (!_nv_f) {\n";
    s += "        fprintf(stderr, \"narval extern Python: nao foi possivel abrir '%s'\\n\", \"" + escaped_path + "\");\n";
    s += "        return;\n";
    s += "    }\n";
    s += "    PyRun_SimpleFile(_nv_f, \"" + escaped_path + "\");\n";
    s += "    fclose(_nv_f);\n";
    s += "    _nv_py_globals = PyModule_GetDict(PyImport_AddModule(\"__main__\"));\n";
    s += "    if (!_nv_py_globals) { PyErr_Print(); }\n";
    s += "}\n\n";

    // Um wrapper por função declarada
    for (auto& decl : node.declarations) {
        // Assinatura: NvValue __extern_X(NvValue arg0_arg, NvValue arg1_arg, ...)
        std::vector<std::pair<std::string, std::string>> params; // (pname, type_str)
        for (auto& param : decl.parameters)
            for (auto& [pname, type_str] : param.parameter)
                params.emplace_back(pname, type_str);

        // Assinatura da função wrapper
        s += "NvValue __extern_" + decl.name + "(";
        if (params.empty()) {
            s += "void";
        } else {
            for (size_t i = 0; i < params.size(); i++) {
                if (i) s += ", ";
                s += "NvValue " + params[i].first + "_arg";
            }
        }
        s += ") {\n";
        s += "    _nv_py_ensure_init();\n";
        s += "    NvValue _nv_result; _nv_result.obj = NULL;\n";
        s += "    if (!_nv_py_globals) { create_option_none(&_nv_result); return _nv_result; }\n";
        s += "    PyObject* _nv_func = PyDict_GetItemString(_nv_py_globals, \"" + decl.name + "\");\n";
        s += "    if (!_nv_func) {\n";
        s += "        fprintf(stderr, \"narval extern Python: funcao '%s' nao encontrada\\n\", \"" + decl.name + "\");\n";
        s += "        create_option_none(&_nv_result); return _nv_result;\n";
        s += "    }\n";

        // Construir tupla de argumentos
        s += "    PyObject* _nv_args = PyTuple_New(" + std::to_string(params.size()) + ");\n";
        for (size_t i = 0; i < params.size(); i++) {
            auto& [pname, type_str] = params[i];
            std::string idx = std::to_string(i);
            if (type_str == "str") {
                s += "    PyTuple_SetItem(_nv_args, " + idx +
                     ", PyUnicode_FromString(nv_extract_string_ptr(&" + pname + "_arg)));\n";
            } else if (type_str == "int" || type_str == "bool") {
                s += "    PyTuple_SetItem(_nv_args, " + idx +
                     ", PyLong_FromLong(extract_int_from_value(&" + pname + "_arg)));\n";
            } else if (type_str == "float") {
                s += "    PyTuple_SetItem(_nv_args, " + idx +
                     ", PyFloat_FromDouble(extract_float_from_value(&" + pname + "_arg)));\n";
            } else {
                // Fallback: passar como string
                s += "    PyTuple_SetItem(_nv_args, " + idx +
                     ", PyUnicode_FromString(nv_extract_string_ptr(&" + pname + "_arg)));\n";
            }
        }

        // Chamar a função Python
        s += "    PyObject* _nv_py_ret = PyObject_Call(_nv_func, _nv_args, NULL);\n";
        s += "    Py_DECREF(_nv_args);\n";
        s += "    if (!_nv_py_ret) { PyErr_Print(); create_option_none(&_nv_result); return _nv_result; }\n";

        // Converter resultado → NvValue
        if (decl.return_type == "str") {
            s += "    const char* _nv_rs = PyUnicode_AsUTF8(_nv_py_ret);\n";
            s += "    create_str(&_nv_result, _nv_rs ? _nv_rs : \"\");\n";
        } else if (decl.return_type == "int") {
            s += "    create_int(&_nv_result, (int)PyLong_AsLong(_nv_py_ret));\n";
        } else if (decl.return_type == "float") {
            s += "    create_float(&_nv_result, PyFloat_AsDouble(_nv_py_ret));\n";
        } else if (decl.return_type == "bool") {
            s += "    create_bool(&_nv_result, PyObject_IsTrue(_nv_py_ret));\n";
        } else {
            // void ou tipo desconhecido
            s += "    create_option_none(&_nv_result);\n";
        }

        s += "    Py_DECREF(_nv_py_ret);\n";
        s += "    return _nv_result;\n";
        s += "}\n\n";
    }

    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Codegen principal
// ─────────────────────────────────────────────────────────────────────────────

void ExternStmtNode::codegen(nv::IRGenerationContext& ctx) {
    auto& M   = ctx.get_module();
    auto& C   = ctx.get_context();
    auto* ValueTy  = nv::ir_utils::get_value_struct(ctx);
    nv::Checker* checker = static_cast<nv::Checker*>(ctx.get_type_checker());

    // ── Caminho Python com source_file: gerar bridge C e compilar ──
    if (language == "Python" && !source_file.empty()) {
        // 1. Gerar o código C do bridge
        std::string bridge_src = generate_python_bridge_c(*this);

        // 2. Escrever em arquivo temporário
        std::string stem = std::filesystem::path(source_file).stem().string();
        std::string c_path  = "narval_py_bridge_" + stem + ".c";
        std::string obj_path = "narval_py_bridge_" + stem + ".o";

        {
            std::ofstream f(c_path);
            f << bridge_src;
        }

        // 3. Compilar o bridge com os headers do Python
        // python3-config --includes retorna algo como: -I/usr/include/python3.12
        std::string py_includes;
        FILE* pipe = popen("python3-config --includes 2>/dev/null", "r");
        if (pipe) {
            char buf[512];
            while (fgets(buf, sizeof(buf), pipe))
                py_includes += buf;
            pclose(pipe);
            // Remover newline final
            while (!py_includes.empty() && (py_includes.back() == '\n' || py_includes.back() == '\r'))
                py_includes.pop_back();
        }

        std::string compile_cmd = "gcc -O2 -fPIC -c " + py_includes + " " +
                                   c_path + " -o " + obj_path + " 2>&1";
        if (system(compile_cmd.c_str()) != 0) {
            llvm::errs() << "narval: falha ao compilar bridge Python (" << c_path << ")\n";
            std::filesystem::remove(c_path);
            return;
        }
        std::filesystem::remove(c_path); // limpar .c temporário

        // 4. Registrar o .o para o linker e a flag -lpython3
        ctx.add_extra_link_item(obj_path);

        // Detectar a libpython via python3-config
        std::string py_ldflags;
        FILE* ld_pipe = popen("python3-config --ldflags --embed 2>/dev/null || python3-config --ldflags 2>/dev/null", "r");
        if (ld_pipe) {
            char buf[512];
            while (fgets(buf, sizeof(buf), ld_pipe))
                py_ldflags += buf;
            pclose(ld_pipe);
            while (!py_ldflags.empty() && (py_ldflags.back() == '\n' || py_ldflags.back() == '\r'))
                py_ldflags.pop_back();
        }
        if (!py_ldflags.empty())
            ctx.add_extra_link_item(py_ldflags);
        else
            ctx.add_extra_link_item("-lpython3");

        // 5. Para cada declaração, criar uma declaração externa no módulo LLVM
        //    O wrapper real está no bridge .o gerado acima
        std::vector<llvm::Type*> wrapper_params(1, ValueTy); // placeholder; real sig abaixo
        for (auto& decl : declarations) {
            std::string wrapper_name = "__extern_" + decl.name;
            if (M.getFunction(wrapper_name)) {
                nv::SymbolInfo info(M.getFunction(wrapper_name),
                                    M.getFunction(wrapper_name)->getType(), nullptr, false, true);
                ctx.get_symbol_table().define_symbol(decl.name, info);
                continue;
            }

            // Todas as funções do bridge têm assinatura: NvValue fn(NvValue, NvValue, ...)
            std::vector<llvm::Type*> sig(decl.parameters.size(), ValueTy);
            for (auto& p : decl.parameters)
                sig.push_back(ValueTy); // um Value por param
            // Reconstruir corretamente:
            sig.clear();
            for (auto& param : decl.parameters)
                for (auto& [pname, type_str] : param.parameter)
                    sig.push_back(ValueTy);

            auto* fn_ty = FunctionType::get(ValueTy, sig, false);
            auto* fn = Function::Create(fn_ty, Function::ExternalLinkage, wrapper_name, M);

            nv::SymbolInfo info(fn, fn->getType(), nullptr, false, true);
            ctx.get_symbol_table().define_symbol(decl.name, info);
        }
        return;
    }

    // ── Caminho padrão: todas as outras linguagens (C, C++, Assembly, Rust, Go) ──
    llvm::Function* prev_fn = ctx.get_current_function();
    llvm::BasicBlock* prev_bb = ctx.get_builder().GetInsertBlock();

    for (auto& decl : declarations) {
        std::string real_name    = decl.name;
        std::string wrapper_name = "__extern_" + decl.name;

        std::vector<std::string> param_type_strs;
        std::vector<llvm::Type*> native_param_types;
        for (auto& param : decl.parameters)
            for (auto& [pname, type_str] : param.parameter) {
                param_type_strs.push_back(type_str);
                native_param_types.push_back(native_type(type_str, ctx));
            }

        llvm::Type* native_ret = native_type(decl.return_type, ctx);

        auto* real_fn = M.getFunction(real_name);
        if (!real_fn) {
            auto* real_fty = FunctionType::get(native_ret, native_param_types, false);
            real_fn = Function::Create(real_fty, Function::ExternalLinkage, real_name, M);
        }

        if (M.getFunction(wrapper_name)) {
            auto* wfn = M.getFunction(wrapper_name);
            nv::SymbolInfo info(wfn, wfn->getType(), nullptr, false, true);
            ctx.get_symbol_table().define_symbol(decl.name, info);
            continue;
        }

        // Wrapper LLVM: Value args → converte → chama C real → empacota resultado
        std::vector<llvm::Type*> wrapper_params(param_type_strs.size(), ValueTy);
        auto* wrapper_fty = FunctionType::get(ValueTy, wrapper_params, false);
        auto* wrapper_fn  = Function::Create(wrapper_fty, Function::InternalLinkage, wrapper_name, M);

        auto* entry_bb = BasicBlock::Create(C, "entry", wrapper_fn);
        IRBuilder<NoFolder> WB(entry_bb);
        auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);

        size_t arg_idx = 0;
        std::vector<Value*> native_args;
        for (auto& arg : wrapper_fn->args()) {
            const std::string& type_str = param_type_strs[arg_idx++];
            auto* slot = WB.CreateAlloca(ValueTy, nullptr, "arg_slot");
            WB.CreateStore(&arg, slot);

            if (type_str == "str") {
                auto* fn = ctx.ensure_runtime_func("nv_extract_string_ptr",
                                                   {ValuePtr}, PointerType::getUnqual(C));
                native_args.push_back(WB.CreateCall(fn, {slot}, "nat_str"));
            } else if (type_str == "int" || type_str == "bool") {
                auto* fn = ctx.ensure_runtime_func("extract_int_from_value",
                                                   {ValuePtr}, Type::getInt32Ty(C));
                native_args.push_back(WB.CreateCall(fn, {slot}, "nat_int"));
            } else if (type_str == "float") {
                auto* fn = ctx.ensure_runtime_func("extract_float_from_value",
                                                   {ValuePtr}, Type::getDoubleTy(C));
                native_args.push_back(WB.CreateCall(fn, {slot}, "nat_flt"));
            } else {
                native_args.push_back(WB.CreateLoad(PointerType::getUnqual(C), slot, "nat_ptr"));
            }
        }

        auto* ret_slot = WB.CreateAlloca(ValueTy, nullptr, "ret_slot");

        if (native_ret->isVoidTy()) {
            WB.CreateCall(real_fn, native_args);
            auto* none_fn = ctx.ensure_runtime_func("create_option_none", {ValuePtr});
            WB.CreateCall(none_fn, {ret_slot});
        } else {
            auto* call_result = WB.CreateCall(real_fn, native_args, "c_ret");
            if (decl.return_type == "int" || decl.return_type == "bool") {
                auto* fn = ctx.ensure_runtime_func("create_int", {ValuePtr, Type::getInt32Ty(C)});
                WB.CreateCall(fn, {ret_slot, call_result});
            } else if (decl.return_type == "float") {
                auto* fn = ctx.ensure_runtime_func("create_float", {ValuePtr, Type::getDoubleTy(C)});
                WB.CreateCall(fn, {ret_slot, call_result});
            } else if (decl.return_type == "str") {
                auto* fn = ctx.ensure_runtime_func("create_str",
                    {ValuePtr, PointerType::getUnqual(C)});
                WB.CreateCall(fn, {ret_slot, call_result});
            } else {
                auto* none_fn = ctx.ensure_runtime_func("create_option_none", {ValuePtr});
                WB.CreateCall(none_fn, {ret_slot});
            }
        }

        WB.CreateRet(WB.CreateLoad(ValueTy, ret_slot, "ret_val"));

        nv::SymbolInfo info(wrapper_fn, wrapper_fn->getType(), nullptr, false, true);
        ctx.get_symbol_table().define_symbol(decl.name, info);
    }

    ctx.set_current_function(prev_fn);
    if (prev_bb) ctx.get_builder().SetInsertPoint(prev_bb);
}
