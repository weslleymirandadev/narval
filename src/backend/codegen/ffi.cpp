#include "backend/codegen/ffi.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/checker/checker.hpp"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/IRBuilder.h"
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <cassert>
#include <unordered_map>

using namespace llvm;

namespace nv::ffi {

// 
//  Tabela de tipos (única fonte de verdade para conversões FFI)
// 

static constexpr TypeDesc TYPE_TABLE[] = {
    //  narval   extract_fn                  box_fn               c_type
    //           py_from                     py_to                py_box
    { "str",
      "nv_extract_string_ptr",   "create_str",        "const char*",
      "PyUnicode_FromString(%s)", "PyUnicode_AsUTF8(%s)",
      "create_str(&_nv_r, PyUnicode_AsUTF8(%s))" },

    { "int",
      "extract_int_from_value",  "create_int",        "int32_t",
      "PyLong_FromLong(%s)",     "(int32_t)PyLong_AsLong(%s)",
      "create_int(&_nv_r, (int32_t)PyLong_AsLong(%s))" },

    { "float",
      "extract_float_from_value","create_float",      "double",
      "PyFloat_FromDouble(%s)",  "PyFloat_AsDouble(%s)",
      "create_float(&_nv_r, PyFloat_AsDouble(%s))" },

    { "bool",
      "extract_int_from_value",  "create_bool",       "int32_t",
      "PyBool_FromLong(%s)",     "(int32_t)PyObject_IsTrue(%s)",
      "create_bool(&_nv_r, (int32_t)PyObject_IsTrue(%s))" },

    { "void",
      "",                        "create_option_none","void",
      "",                        "",
      "create_option_none(&_nv_r)" },

    // Tipos compostos: extract_fn vazio indica uso do conversor genérico _nv_to_pyobj.
    // py_from: %s = nome da variável NvValue (sem &); py_box: %s = PyObject* resultado.
    { "vector",
      "",                        "create_vector",     "void*",
      "_nv_to_pyobj(&%s)",       "",
      "_nv_from_pyobj(&_nv_r, %s)" },

    { "map",
      "",                        "create_map",        "void*",
      "_nv_to_pyobj(&%s)",       "",
      "_nv_from_pyobj(&_nv_r, %s)" },

    { "tuple",
      "",                        "create_vector",     "void*",
      "_nv_to_pyobj(&%s)",       "",
      "_nv_from_pyobj(&_nv_r, %s)" },
};

static constexpr std::string_view FALLBACK_TYPE = "str";

const TypeDesc& type_desc(std::string_view narval_type) {
    for (const auto& d : TYPE_TABLE)
        if (d.narval == narval_type) return d;
    // Fallback transparente: trata tipo desconhecido como str
    return type_desc(FALLBACK_TYPE);
}

llvm::Type* llvm_native_type(std::string_view narval_type, llvm::LLVMContext& C) {
    if (narval_type == "str")   return llvm::PointerType::getUnqual(C);
    if (narval_type == "int")   return llvm::Type::getInt32Ty(C);
    if (narval_type == "float") return llvm::Type::getDoubleTy(C);
    if (narval_type == "bool")  return llvm::Type::getInt32Ty(C);
    if (narval_type == "void")  return llvm::Type::getVoidTy(C);
    // Tipos compostos (vector, map, tuple): passados como Value* no C-ABI
    return llvm::PointerType::getUnqual(C);
}

// 
//  Helpers livres compartilhados pelos backends e funções standalone
// 

// Executa um comando shell e retorna stdout sem trailing newline.
static std::string shell_output(const std::string& cmd) {
    std::string out;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";
    char buf[512];
    while (fgets(buf, sizeof(buf), p)) out += buf;
    pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

static std::string python_ldflags() {
    std::string f = shell_output("python3-config --ldflags --embed 2>/dev/null");
    if (f.empty()) f = shell_output("python3-config --ldflags 2>/dev/null");
    return f.empty() ? "-lpython3" : f;
}

// 
//  Helpers de geração de código — compartilhados pelos backends
// 

// Substitui o primeiro %s em `fmt` por `value`.
static std::string fmt1(std::string_view fmt, std::string_view value) {
    std::string s(fmt);
    auto pos = s.find("%s");
    if (pos != std::string::npos) s.replace(pos, 2, value);
    return s;
}

// Coleta (pname, type_str) de todos os parâmetros de uma declaração.
static std::vector<std::pair<std::string,std::string>>
collect_params(const ExternFuncDecl& decl) {
    std::vector<std::pair<std::string,std::string>> out;
    for (const auto& param : decl.parameters)
        for (const auto& [k, v] : param.parameter)
            out.emplace_back(k, v);
    return out;
}

// 
//  CABIBackend — C / C++ / Assembly / Rust / Go (e Python sem source_file)
//
//  Gera dois artefatos por declaração:
//    1. Declaração externa LLVM da função C real (tipos nativos).
//    2. Wrapper interno LLVM com parâmetros Value que extrai, chama e box.
// 

class CABIBackend final : public Backend {
public:
    void emit(const ExternStmtNode& node, IRGenerationContext& ctx) override {
        auto& M  = ctx.get_module();
        auto& C  = ctx.get_context();
        auto* VT = ir_utils::get_value_struct(ctx);
        auto* VP = ir_utils::get_value_ptr(ctx);

        llvm::Function* prev_fn = ctx.get_current_function();
        llvm::BasicBlock* prev_bb = ctx.get_builder().GetInsertBlock();

        for (const auto& decl : node.declarations) {
            const std::string real_name    = decl.name;
            const std::string wrapper_name = "__extern_" + decl.name;

            // Coletar tipos dos parâmetros
            auto params = collect_params(decl);

            //  1. Declarar a função C real com tipos nativos 
            if (!M.getFunction(real_name)) {
                std::vector<llvm::Type*> native_params;
                for (const auto& [_, t] : params)
                    native_params.push_back(llvm_native_type(t, C));
                auto* native_ret = llvm_native_type(decl.return_type, C);
                auto* fn_ty = llvm::FunctionType::get(native_ret, native_params, false);
                llvm::Function::Create(fn_ty, llvm::Function::ExternalLinkage, real_name, M);
            }
            auto* real_fn = M.getFunction(real_name);

            //  2. Gerar wrapper LLVM (se ainda não existir) 
            if (M.getFunction(wrapper_name)) {
                register_symbol(wrapper_name, decl.name, M, ctx);
                continue;
            }

            std::vector<llvm::Type*> wrapper_sig(params.size(), VT);
            auto* wrapper_fty = llvm::FunctionType::get(VT, wrapper_sig, false);
            auto* wfn = llvm::Function::Create(wrapper_fty, llvm::Function::InternalLinkage, wrapper_name, M);

            llvm::IRBuilder<llvm::NoFolder> WB(llvm::BasicBlock::Create(C, "entry", wfn));
            auto* ret_slot = WB.CreateAlloca(VT, nullptr, "ret");

            // Converter cada arg Value → tipo nativo
            std::vector<llvm::Value*> native_args;
            size_t idx = 0;
            for (auto& arg : wfn->args()) {
                const auto& td = type_desc(params[idx++].second);
                auto* slot = WB.CreateAlloca(VT, nullptr, "arg");
                WB.CreateStore(&arg, slot);
                if (!td.extract_fn.empty()) {
                    auto* fn = ctx.ensure_runtime_func(std::string(td.extract_fn),
                                                       {VP}, real_fn->getFunctionType()->getParamType(idx-1));
                    native_args.push_back(WB.CreateCall(fn, {slot}));
                } else {
                    // Tipo composto (vector, map, etc.): passar Value* diretamente
                    native_args.push_back(slot);
                }
            }

            // Chamar a função C real
            if (real_fn->getReturnType()->isVoidTy()) {
                WB.CreateCall(real_fn, native_args);
                box_void(WB, ctx, VP, ret_slot);
            } else {
                auto* result = WB.CreateCall(real_fn, native_args, "c_ret");
                box_native(WB, ctx, decl.return_type, result, VP, VT, ret_slot);
            }

            WB.CreateRet(WB.CreateLoad(VT, ret_slot, "ret_val"));

            register_symbol(wrapper_name, decl.name, M, ctx);
        }

        ctx.set_current_function(prev_fn);
        if (prev_bb) ctx.get_builder().SetInsertPoint(prev_bb);
    }

private:
    void box_void(IRBuilder<NoFolder>& B, IRGenerationContext& ctx,
                  llvm::Type* VP, Value* ret_slot) {
        auto* fn = ctx.ensure_runtime_func("create_option_none", {VP});
        B.CreateCall(fn, {ret_slot});
    }

    void box_native(IRBuilder<NoFolder>& B, IRGenerationContext& ctx,
                    const std::string& narval_type, Value* native_val,
                    llvm::Type* VP, llvm::Type* VT, Value* ret_slot) {
        const auto& td = type_desc(narval_type);
        if (td.box_fn == "create_option_none" || td.box_fn.empty()) {
            box_void(B, ctx, VP, ret_slot);
            return;
        }
        auto* fn = ctx.ensure_runtime_func(std::string(td.box_fn), {VP, native_val->getType()});
        B.CreateCall(fn, {ret_slot, native_val});
    }

    void register_symbol(const std::string& wrapper_name, const std::string& orig_name,
                         llvm::Module& M, IRGenerationContext& ctx) {
        auto* fn = M.getFunction(wrapper_name);
        if (!fn) return;
        SymbolInfo info(fn, fn->getType(), nullptr, false, true);
        ctx.get_symbol_table().define_symbol(orig_name, info);
    }
};

// 
//  PythonBackend — `extern "Python" from "arquivo.py" { ... }`
//
//  Gera um arquivo C bridge que:
//    - Inicializa Python lazy (Py_Initialize + carrega o .py)
//    - Para cada função: extrai args, monta PyTuple, chama PyObject_Call, box resultado
//
//  O bridge é compilado com gcc e os artefatos adicionados ao link.
//  Adicionar suporte a Lua/Ruby/etc.: copiar este backend e adaptar o template.
// 

class PythonBackend final : public Backend {
public:
    void emit(const ExternStmtNode& node, IRGenerationContext& ctx) override {
        const std::string stem = std::filesystem::path(node.source_file).stem().string();
        const std::string bridge_c   = "narval_py_bridge_" + stem + ".c";
        const std::string bridge_obj = "narval_py_bridge_" + stem + ".o";

        // Escrever e compilar o bridge
        { std::ofstream f(bridge_c); f << build_bridge_source(node); }
        if (!compile_bridge(bridge_c, bridge_obj)) return;
        std::filesystem::remove(bridge_c);

        ctx.add_extra_link_item(bridge_obj);
        ctx.add_extra_link_item(python_ldflags());

        // Declarar os wrappers no módulo LLVM como symbols externos
        declare_wrappers(node, ctx);
    }

private:
    //  Geração do código C bridge (data-driven via TYPE_TABLE) 

    std::string build_bridge_source(const ExternStmtNode& node) const {
        std::string s;
        s += bridge_header();
        s += init_function(node.source_file);
        for (const auto& decl : node.declarations)
            s += wrapper_function(decl);
        return s;
    }

    static std::string bridge_header() {
        // Layout mínimo dos structs internos do runtime Narval (estável, gerado em C).
        // Usado pelos conversores genéricos _nv_to_pyobj / _nv_from_pyobj.
        return
            "#define PY_SSIZE_T_CLEAN\n"
            "#include <Python.h>\n"
            "#include <stdio.h>\n"
            "#include <stdint.h>\n"
            "#include <stddef.h>\n\n"

            // Tipos opacos do runtime Narval
            "typedef struct { void* obj; } NvValue;\n"
            "typedef struct { void* ob_type; int32_t ref_count; uint32_t flags; } _NvObj;\n"
            "typedef struct { _NvObj base; char* value; size_t len; } _NvStr;\n"
            "typedef struct { _NvObj base; int32_t value; } _NvInt;\n"
            "typedef struct { _NvObj base; double value; } _NvFlt;\n"
            "typedef struct { _NvObj base; NvValue* elements; int size; int capacity; } _NvVec;\n"
            "typedef struct { _NvObj base; char** keys; NvValue* values; int size; int capacity; void* str_m; } _NvMap;\n\n"

            // Ponteiros globais de tipo (definidos em builtin_classes.c)
            "extern void* NVStr_Type;\n"
            "extern void* NVInt_Type;\n"
            "extern void* NVFloat_Type;\n"
            "extern void* NVBool_Type;\n"
            "extern void* NVVector_Type;\n"
            "extern void* NVArray_Type;\n"
            "extern void* NVMap_Type;\n\n"

            // Funções do runtime Narval
            "extern void create_str(NvValue*, const char*);\n"
            "extern void create_int(NvValue*, int32_t);\n"
            "extern void create_float(NvValue*, double);\n"
            "extern void create_bool(NvValue*, int32_t);\n"
            "extern void create_vector(NvValue*, int32_t);\n"
            "extern void create_option_none(NvValue*);\n"
            "extern const char* nv_extract_string_ptr(NvValue*);\n"
            "extern int32_t     extract_int_from_value(NvValue*);\n"
            "extern double      extract_float_from_value(NvValue*);\n"
            "extern void        vector_push_method(NvValue*, NvValue*, NvValue*);\n\n"

            //  Conversor genérico NvValue → PyObject* 
            // Suporta: str, int, float, bool, vector/array (→ list), map (→ dict).
            // Chamado recursivamente para elementos de coleções.
            "static PyObject* _nv_to_pyobj(NvValue* v) {\n"
            "    if (!v || !v->obj) Py_RETURN_NONE;\n"
            "    _NvObj* o = (_NvObj*)v->obj;\n"
            "    if (o->ob_type == NVStr_Type)   { _NvStr* s = (_NvStr*)o; return PyUnicode_FromString(s->value ? s->value : \"\"); }\n"
            "    if (o->ob_type == NVBool_Type)  { _NvInt* b = (_NvInt*)o; return PyBool_FromLong(b->value); }\n"
            "    if (o->ob_type == NVInt_Type)   { _NvInt* i = (_NvInt*)o; return PyLong_FromLong(i->value); }\n"
            "    if (o->ob_type == NVFloat_Type) { _NvFlt* f = (_NvFlt*)o; return PyFloat_FromDouble(f->value); }\n"
            "    if (o->ob_type == NVVector_Type || o->ob_type == NVArray_Type) {\n"
            "        _NvVec* vec = (_NvVec*)o;\n"
            "        PyObject* lst = PyList_New(vec->size);\n"
            "        for (int i = 0; i < vec->size; i++)\n"
            "            PyList_SET_ITEM(lst, i, _nv_to_pyobj(&vec->elements[i]));\n"
            "        return lst;\n"
            "    }\n"
            "    if (o->ob_type == NVMap_Type) {\n"
            "        _NvMap* m = (_NvMap*)o;\n"
            "        PyObject* d = PyDict_New();\n"
            "        for (int i = 0; i < m->size; i++) {\n"
            "            PyObject* val = _nv_to_pyobj(&m->values[i]);\n"
            "            PyDict_SetItemString(d, m->keys[i] ? m->keys[i] : \"\", val);\n"
            "            Py_DECREF(val);\n"
            "        }\n"
            "        return d;\n"
            "    }\n"
            "    Py_RETURN_NONE;\n"
            "}\n\n"

            //  Conversor genérico PyObject* → NvValue 
            // Suporta: str, int, float, bool, list/tuple (→ vector), dict (→ map).
            "static void _nv_from_pyobj(NvValue* out, PyObject* obj) {\n"
            "    if (!obj || obj == Py_None) { create_option_none(out); return; }\n"
            "    if (PyUnicode_Check(obj)) { const char* s = PyUnicode_AsUTF8(obj); create_str(out, s ? s : \"\"); return; }\n"
            "    if (PyBool_Check(obj))    { create_bool(out, (int32_t)PyObject_IsTrue(obj)); return; }\n"
            "    if (PyLong_Check(obj))    { create_int(out, (int32_t)PyLong_AsLong(obj)); return; }\n"
            "    if (PyFloat_Check(obj))   { create_float(out, PyFloat_AsDouble(obj)); return; }\n"
            "    if (PyList_Check(obj) || PyTuple_Check(obj)) {\n"
            "        Py_ssize_t n = PySequence_Size(obj);\n"
            "        create_vector(out, (int32_t)n);\n"
            "        NvValue _tmp; _tmp.obj = NULL;\n"
            "        for (Py_ssize_t i = 0; i < n; i++) {\n"
            "            PyObject* item = PySequence_GetItem(obj, i);\n"
            "            NvValue elem; elem.obj = NULL;\n"
            "            _nv_from_pyobj(&elem, item);\n"
            "            Py_DECREF(item);\n"
            "            vector_push_method(&_tmp, out, &elem);\n"
            "        }\n"
            "        return;\n"
            "    }\n"
            "    create_option_none(out);\n"
            "}\n\n"

            "static PyObject* _nv_py_globals = NULL;\n\n";
    }

    static std::string init_function(const std::string& path) {
        std::string ep;
        for (char c : path)
            ep += (c == '\\') ? "\\\\" : (c == '"') ? "\\\"" : std::string(1, c);

        return
            "#include <stdlib.h>\n\n"
            "static void _nv_py_ensure_init(void) {\n"
            "    if (_nv_py_globals) return;\n"
            "    Py_Initialize();\n"
            // Se há um venv ativo (VIRTUAL_ENV está definido), adicionar seu site-packages
            // ao sys.path para que pacotes do venv (matplotlib, numpy, etc.) sejam encontrados.
            "    const char* _venv = getenv(\"VIRTUAL_ENV\");\n"
            "    if (_venv) {\n"
            "        char _cmd[1024];\n"
            "        snprintf(_cmd, sizeof(_cmd),\n"
            "            \"import sys, os\\n\"\n"
            "            \"_venv='%s'\\n\"\n"
            "            \"for _e in os.listdir(os.path.join(_venv,'lib')):\\n\"\n"
            "            \"    if _e.startswith('python'):\\n\"\n"
            "            \"        _sp=os.path.join(_venv,'lib',_e,'site-packages')\\n\"\n"
            "            \"        if _sp not in sys.path: sys.path.insert(0,_sp)\\n\"\n"
            "            \"        break\\n\"\n"
            "            , _venv);\n"
            "        PyRun_SimpleString(_cmd);\n"
            "    }\n"
            "    FILE* _f = fopen(\"" + ep + "\", \"r\");\n"
            "    if (!_f) { fprintf(stderr, \"narval extern Python: cannot open '%s'\\n\","
            " \"" + ep + "\"); return; }\n"
            "    PyRun_SimpleFile(_f, \"" + ep + "\");\n"
            "    fclose(_f);\n"
            "    _nv_py_globals = PyModule_GetDict(PyImport_AddModule(\"__main__\"));\n"
            "    if (!_nv_py_globals) PyErr_Print();\n"
            "}\n\n";
    }

    // Gera um wrapper C para uma função declarada.
    // O mapeamento de tipos é inteiramente guiado pela TYPE_TABLE.
    static std::string wrapper_function(const ExternFuncDecl& decl) {
        auto params = collect_params(decl);
        std::string s;

        // Assinatura: NvValue __extern_X(NvValue arg0, ...)
        s += "NvValue __extern_" + decl.name + "(";
        for (size_t i = 0; i < params.size(); ++i) {
            if (i) s += ", ";
            s += "NvValue " + params[i].first + "_nv";
        }
        s += params.empty() ? "void" : "";
        s += ") {\n";
        s += "    _nv_py_ensure_init();\n";
        s += "    NvValue _nv_r; _nv_r.obj = NULL;\n";
        s += "    if (!_nv_py_globals) { create_option_none(&_nv_r); return _nv_r; }\n";

        // Lookup da função Python
        s += "    PyObject* _fn = PyDict_GetItemString(_nv_py_globals, \""
             + decl.name + "\");\n";
        s += "    if (!_fn) { fprintf(stderr, \"narval extern Python: function '"
             + decl.name + "' not found\\n\"); create_option_none(&_nv_r); return _nv_r; }\n";

        // Construção da tupla de argumentos (data-driven via TYPE_TABLE)
        s += "    PyObject* _args = PyTuple_New(" + std::to_string(params.size()) + ");\n";
        for (size_t i = 0; i < params.size(); ++i) {
            const auto& [pname, type_str] = params[i];
            const auto& td = type_desc(type_str);

            std::string py_obj;
            if (td.extract_fn.empty()) {
                // Tipo composto (vector, map, tuple): py_from recebe o nome da variável NvValue
                // ex: "_nv_to_pyobj(&x_nv)"
                py_obj = fmt1(td.py_from, pname + "_nv");
            } else {
                // Tipo primitivo: extrair nativo e depois converter para PyObject*
                // ex: "PyLong_FromLong(extract_int_from_value(&n_nv))"
                std::string native = std::string(td.extract_fn) + "(&" + pname + "_nv)";
                py_obj = fmt1(td.py_from, native);
            }
            s += "    PyTuple_SetItem(_args, " + std::to_string(i) + ", " + py_obj + ");\n";
        }

        // Chamada
        s += "    PyObject* _ret = PyObject_Call(_fn, _args, NULL);\n";
        s += "    Py_DECREF(_args);\n";
        s += "    if (!_ret) { PyErr_Print(); create_option_none(&_nv_r); return _nv_r; }\n";

        // Converter resultado → NvValue (data-driven via py_box)
        const auto& ret_td = type_desc(decl.return_type);
        if (!ret_td.py_box.empty())
            s += "    " + fmt1(ret_td.py_box, "_ret") + ";\n";
        else
            s += "    create_option_none(&_nv_r);\n";

        s += "    Py_DECREF(_ret);\n";
        s += "    return _nv_r;\n}\n\n";
        return s;
    }

    //  Compilação e linking 

    bool compile_bridge(const std::string& src, const std::string& obj) const {
        std::string includes = shell_output("python3-config --includes 2>/dev/null");
        std::string cmd = "gcc -O2 -fPIC -c " + includes + " " + src + " -o " + obj + " 2>&1";
        if (system(cmd.c_str()) != 0) {
            llvm::errs() << "narval: failed to compile Python bridge (" << src << ")\n";
            std::filesystem::remove(src);
            return false;
        }
        return true;
    }

    //  Declaração dos wrappers no módulo LLVM 

    static void declare_wrappers(const ExternStmtNode& node, IRGenerationContext& ctx) {
        auto& M  = ctx.get_module();
        auto& C  = ctx.get_context();
        auto* VT = ir_utils::get_value_struct(ctx);

        for (const auto& decl : node.declarations) {
            const std::string wname = "__extern_" + decl.name;
            if (!M.getFunction(wname)) {
                auto params = collect_params(decl);
                std::vector<llvm::Type*> sig(params.size(), VT);
                auto* fty = llvm::FunctionType::get(VT, sig, false);
                llvm::Function::Create(fty, llvm::Function::ExternalLinkage, wname, M);
            }
            auto* fn = M.getFunction(wname);
            SymbolInfo info(fn, fn->getType(), nullptr, false, true);
            ctx.get_symbol_table().define_symbol(decl.name, info);
        }
    }

};

// 
//  Registry / factory
//
//  Para registrar uma nova linguagem:
//    1. Crie um backend (ex: LuaBackend)
//    2. Adicione um `static LuaBackend lua_backend;`
//    3. Adicione a condição em backend_for()
// 

Backend& backend_for(const std::string& language, const std::string& source_file) {
    static PythonBackend python_backend;
    static CABIBackend   cabi_backend;

    if (language == "Python" && !source_file.empty())
        return python_backend;

    // C, C++, Assembly, Rust, Go — e Python sem source_file (bridge manual do usuário)
    return cabi_backend;
}

// 
//  Registry de assinaturas de bibliotecas C padrão
//
//  Adicionar uma nova biblioteca: criar entrada em C_REGISTRY.
//  Adicionar uma nova função: incluir no vetor da biblioteca correspondente.
// 

static const std::unordered_map<std::string, std::vector<CLibFunc>>& get_c_registry() {
    static const std::unordered_map<std::string, std::vector<CLibFunc>> C_REGISTRY = {
        {"math", {
            {"sin",   "float", {{"x", "float"}}},
            {"cos",   "float", {{"x", "float"}}},
            {"tan",   "float", {{"x", "float"}}},
            {"asin",  "float", {{"x", "float"}}},
            {"acos",  "float", {{"x", "float"}}},
            {"atan",  "float", {{"x", "float"}}},
            {"atan2", "float", {{"y", "float"}, {"x", "float"}}},
            {"sqrt",  "float", {{"x", "float"}}},
            {"cbrt",  "float", {{"x", "float"}}},
            {"pow",   "float", {{"base", "float"}, {"exp", "float"}}},
            {"exp",   "float", {{"x", "float"}}},
            {"log",   "float", {{"x", "float"}}},
            {"log10", "float", {{"x", "float"}}},
            {"log2",  "float", {{"x", "float"}}},
            {"floor", "float", {{"x", "float"}}},
            {"ceil",  "float", {{"x", "float"}}},
            {"round", "float", {{"x", "float"}}},
            {"fabs",  "float", {{"x", "float"}}},
            {"fmod",  "float", {{"x", "float"}, {"y", "float"}}},
            {"hypot", "float", {{"x", "float"}, {"y", "float"}}},
        }},
        {"stdlib", {
            {"rand",   "int",   {}},
            {"srand",  "void",  {{"seed", "int"}}},
            {"abs",    "int",   {{"x", "int"}}},
            {"atoi",   "int",   {{"s", "str"}}},
            {"atof",   "float", {{"s", "str"}}},
            {"exit",   "void",  {{"code", "int"}}},
            {"malloc", "int",   {{"size", "int"}}},
            {"free",   "void",  {{"ptr", "int"}}},
        }},
        {"string", {
            {"strlen",  "int",  {{"s", "str"}}},
            {"strcmp",  "int",  {{"a", "str"}, {"b", "str"}}},
            {"strncmp", "int",  {{"a", "str"}, {"b", "str"}, {"n", "int"}}},
            {"strchr",  "str",  {{"s", "str"}, {"c", "int"}}},
            {"strstr",  "str",  {{"haystack", "str"}, {"needle", "str"}}},
        }},
        {"stdio", {
            {"printf",  "int",  {{"fmt", "str"}}},
            {"puts",    "int",  {{"s", "str"}}},
            {"putchar", "int",  {{"c", "int"}}},
            {"getchar", "int",  {}},
        }},
        {"time", {
            {"time",  "int", {{"t", "int"}}},
            {"clock", "int", {}},
        }},
    };
    return C_REGISTRY;
}

std::vector<CLibFunc> c_lib_registry(const std::string& libname) {
    const auto& reg = get_c_registry();
    auto it = reg.find(libname);
    if (it != reg.end()) return it->second;
    return {};
}

void emit_c_func(const CLibFunc& fn, IRGenerationContext& ctx) {
    ExternFuncDecl decl(fn.name, {}, fn.return_type);
    for (auto& [pname, ptype] : fn.params) {
        std::unordered_map<std::string,std::string> pm;
        pm[pname] = ptype;
        decl.parameters.emplace_back(pm, nullptr);
    }
    ExternStmtNode tmp_node("C", {decl});
    backend_for("C", "").emit(tmp_node, ctx);
}

// 
//  Python namespace: `from extern "Python:mod" import * as alias`
//
//  Gera bridge C com:
//    static PyObject* _nv_py_ns_ALIAS         — referência ao módulo importado
//    NvValue _nv_py_ns_call_ALIAS(i8*, Value**, i32)  — dispatcher genérico
//
//  Declara o dispatcher no módulo LLVM e registra `alias` como py_namespace.
// 

void emit_python_namespace(const std::string& module, const std::string& alias,
                            IRGenerationContext& ctx) {
    const std::string stem      = "ns_" + alias;
    const std::string bridge_c  = "narval_py_bridge_" + stem + ".c";
    const std::string bridge_o  = "narval_py_bridge_" + stem + ".o";
    const std::string disp_name = "_nv_py_ns_call_" + alias;

    //  Gerar bridge C 
    std::string src;

    // Reutilizar o header genérico do PythonBackend (types, converters, init)
    // Mas precisamos de uma versão simplificada aqui — inline os elementos essenciais
    src +=
        "#define PY_SSIZE_T_CLEAN\n#include <Python.h>\n"
        "#include <stdio.h>\n#include <stdlib.h>\n#include <stdint.h>\n\n"
        "typedef struct { void* obj; } NvValue;\n"
        "typedef struct { void* ob_type; int32_t ref_count; uint32_t flags; } _NvObj;\n"
        "typedef struct { _NvObj base; char* value; size_t len; } _NvStr;\n"
        "typedef struct { _NvObj base; int32_t value; } _NvInt;\n"
        "typedef struct { _NvObj base; double value; } _NvFlt;\n"
        "typedef struct { _NvObj base; NvValue* elements; int size; int capacity; } _NvVec;\n\n"
        "extern void* NVStr_Type; extern void* NVInt_Type;\n"
        "extern void* NVFloat_Type; extern void* NVBool_Type;\n"
        "extern void* NVVector_Type; extern void* NVArray_Type;\n"
        "extern void create_str(NvValue*,const char*); extern void create_int(NvValue*,int32_t);\n"
        "extern void create_float(NvValue*,double);    extern void create_bool(NvValue*,int32_t);\n"
        "extern void create_option_none(NvValue*);\n"
        "extern void create_vector(NvValue*,int32_t);\n"
        "extern void vector_push_method(NvValue*,NvValue*,NvValue*);\n\n";

    // Conversor genérico (inline para evitar conflito com outros bridges)
    src +=
        "static PyObject* _nv_to_py_" + alias + "(NvValue* v) {\n"
        "    if (!v||!v->obj) Py_RETURN_NONE;\n"
        "    _NvObj* o=(_NvObj*)v->obj;\n"
        "    if(o->ob_type==NVStr_Type)   {_NvStr*s=(_NvStr*)o;return PyUnicode_FromString(s->value?s->value:\"\");}\n"
        "    if(o->ob_type==NVBool_Type)  {_NvInt*b=(_NvInt*)o;return PyBool_FromLong(b->value);}\n"
        "    if(o->ob_type==NVInt_Type)   {_NvInt*i=(_NvInt*)o;return PyLong_FromLong(i->value);}\n"
        "    if(o->ob_type==NVFloat_Type) {_NvFlt*f=(_NvFlt*)o;return PyFloat_FromDouble(f->value);}\n"
        "    if(o->ob_type==NVVector_Type||o->ob_type==NVArray_Type){\n"
        "        _NvVec*vec=(_NvVec*)o;PyObject*lst=PyList_New(vec->size);\n"
        "        for(int i=0;i<vec->size;i++) PyList_SET_ITEM(lst,i,_nv_to_py_" + alias + "(&vec->elements[i]));\n"
        "        return lst;}\n"
        "    Py_RETURN_NONE;}\n\n";

    src +=
        "static void _nv_from_py_" + alias + "(NvValue* out, PyObject* obj) {\n"
        "    if(!obj||obj==Py_None){create_option_none(out);return;}\n"
        "    if(PyUnicode_Check(obj)){const char*s=PyUnicode_AsUTF8(obj);create_str(out,s?s:\"\");return;}\n"
        "    if(PyBool_Check(obj)){create_bool(out,(int32_t)PyObject_IsTrue(obj));return;}\n"
        "    if(PyLong_Check(obj)){create_int(out,(int32_t)PyLong_AsLong(obj));return;}\n"
        "    if(PyFloat_Check(obj)){create_float(out,PyFloat_AsDouble(obj));return;}\n"
        "    if(PyList_Check(obj)||PyTuple_Check(obj)){\n"
        "        Py_ssize_t n=PySequence_Size(obj);create_vector(out,(int32_t)n);\n"
        "        NvValue _t;_t.obj=NULL;\n"
        "        for(Py_ssize_t i=0;i<n;i++){PyObject*it=PySequence_GetItem(obj,i);\n"
        "            NvValue e;e.obj=NULL;_nv_from_py_" + alias + "(&e,it);Py_DECREF(it);\n"
        "            vector_push_method(&_t,out,&e);}return;}\n"
        "    create_option_none(out);}\n\n";

    // Módulo global + init
    src += "static PyObject* _nv_py_ns_" + alias + " = NULL;\n\n";

    // Escape do nome do módulo para C string
    std::string escaped_module;
    for (char c : module)
        escaped_module += (c == '"') ? "\\\"" : std::string(1, c);

    // Usar um sentinel distinto de NULL para saber que a init já rodou mas falhou,
    // evitando o retry loop: cada method call retentaria a init se ns==NULL.
    src += "static int _nv_py_ns_tried_" + alias + " = 0;\n\n";

    src +=
        "static void _nv_py_ns_init_" + alias + "(void) {\n"
        "    if (_nv_py_ns_" + alias + " || _nv_py_ns_tried_" + alias + ") return;\n"
        "    _nv_py_ns_tried_" + alias + " = 1;\n"
        // Configurar PYTHONPATH com o site-packages do venv ANTES de Py_Initialize(),
        // usando PY_MAJOR_VERSION e PY_MINOR_VERSION do Python linkado.
        "    const char* _venv = getenv(\"VIRTUAL_ENV\");\n"
        "    if (_venv && !Py_IsInitialized()) {\n"
        "        char _sppath[1024];\n"
        "        snprintf(_sppath, sizeof(_sppath), \"%s/lib/python%d.%d/site-packages\",\n"
        "                 _venv, PY_MAJOR_VERSION, PY_MINOR_VERSION);\n"
        "        const char* _existing = getenv(\"PYTHONPATH\");\n"
        "        char _fullpath[2048];\n"
        "        if (_existing && _existing[0])\n"
        "            snprintf(_fullpath, sizeof(_fullpath), \"%s:%s\", _sppath, _existing);\n"
        "        else\n"
        "            snprintf(_fullpath, sizeof(_fullpath), \"%s\", _sppath);\n"
        "        setenv(\"PYTHONPATH\", _fullpath, 1);\n"
        "    }\n"
        "    if (!Py_IsInitialized()) Py_Initialize();\n"
        // Belt-and-suspenders: também adicionar via sys.path após init
        "    if (_venv) {\n"
        "        char _cmd[512];\n"
        "        snprintf(_cmd, sizeof(_cmd),\n"
        "            \"import sys\\n\"\n"
        "            \"_sp='%s/lib/python%d.%d/site-packages'\\n\"\n"
        "            \"if _sp not in sys.path: sys.path.insert(0,_sp)\\n\",\n"
        "            _venv, PY_MAJOR_VERSION, PY_MINOR_VERSION);\n"
        "        PyRun_SimpleString(_cmd);\n"
        "    }\n"
        "    _nv_py_ns_" + alias + " = PyImport_ImportModule(\"" + escaped_module + "\");\n"
        "    if (!_nv_py_ns_" + alias + ") { PyErr_Print();\n"
        "        fprintf(stderr,\"narval: cannot import Python module '" + escaped_module + "'\\n\"); }\n"
        "}\n\n";

    // Dispatcher genérico: NvValue _nv_py_ns_call_ALIAS(const char* method, NvValue** args, int n)
    src +=
        "NvValue " + disp_name + "(const char* method, NvValue** args, int n) {\n"
        "    _nv_py_ns_init_" + alias + "();\n"
        "    NvValue _r; _r.obj = NULL;\n"
        "    if (!_nv_py_ns_" + alias + ") { create_option_none(&_r); return _r; }\n"
        "    PyObject* _fn = PyObject_GetAttrString(_nv_py_ns_" + alias + ", method);\n"
        "    if (!_fn) { PyErr_Clear(); create_option_none(&_r); return _r; }\n"
        "    PyObject* _tup = PyTuple_New(n);\n"
        "    for (int i = 0; i < n; i++)\n"
        "        PyTuple_SET_ITEM(_tup, i, _nv_to_py_" + alias + "(args[i]));\n"
        "    PyObject* _ret = PyObject_Call(_fn, _tup, NULL);\n"
        "    Py_DECREF(_tup); Py_DECREF(_fn);\n"
        "    if (!_ret) { PyErr_Print(); create_option_none(&_r); return _r; }\n"
        "    _nv_from_py_" + alias + "(&_r, _ret);\n"
        "    Py_DECREF(_ret);\n"
        "    return _r;\n"
        "}\n";

    //  Compilar 
    { std::ofstream f(bridge_c); f << src; }

    std::string py_inc = shell_output("python3-config --includes 2>/dev/null");
    std::string cmd = "gcc -O2 -fPIC -c " + py_inc + " " + bridge_c + " -o " + bridge_o + " 2>&1";
    if (system(cmd.c_str()) != 0) {
        llvm::errs() << "narval: failed to compile Python namespace bridge (" << bridge_c << ")\n";
        std::filesystem::remove(bridge_c);
        return;
    }
    std::filesystem::remove(bridge_c);

    ctx.add_extra_link_item(bridge_o);
    ctx.add_extra_link_item(python_ldflags());

    //  Declarar dispatcher no módulo LLVM 
    auto& M   = ctx.get_module();
    auto& C   = ctx.get_context();
    auto* VT  = ir_utils::get_value_struct(ctx);
    auto* I8P = llvm::PointerType::getUnqual(C);
    auto* I32 = llvm::Type::getInt32Ty(C);
    // Value** — ponteiro para array de Value*
    auto* VPP = llvm::PointerType::getUnqual(C); // opaque ptr (LLVM 15+)

    if (!M.getFunction(disp_name)) {
        auto* fty = llvm::FunctionType::get(VT, {I8P, VPP, I32}, false);
        llvm::Function::Create(fty, llvm::Function::ExternalLinkage, disp_name, M);
    }

    // Registrar alias como py_namespace no contexto para interceptação em call_expr
    ctx.register_py_namespace(alias);
    SymbolInfo info(M.getFunction(disp_name),
                    M.getFunction(disp_name)->getType(), nullptr, false, true);
    ctx.get_symbol_table().define_symbol(alias, info);
}

void emit_python_module_funcs(const std::string& module,
                               const std::vector<std::pair<std::string,std::string>>& funcs,
                               IRGenerationContext& ctx) {
    // Construir um ExternFuncDecl por função importada do módulo Python.
    // Todos os tipos são genéricos (str→str): o bridge inferirá em runtime.
    // Para tipagem precisa, o usuário pode usar extern "Python" from "file.py" { def ... }.
    std::vector<ExternFuncDecl> decls;
    for (auto& [orig, alias] : funcs) {
        std::string sym = alias.empty() ? orig : alias;
        // Sem parâmetros declarados explicitamente → bridge aceitará qualquer arity via varargs
        ExternFuncDecl d(orig, {}, "void");
        decls.push_back(std::move(d));
    }

    // Gerar bridge equivalente a um extern "Python" from "<module-as-virtual-file.py>"
    // usando o PythonBackend, mas importando via PyImport ao invés de PyRun_SimpleFile.
    // Implementação simplificada: gerar um arquivo .py virtual que re-exporta as funções.
    const std::string stem = "mod_" + module;
    const std::string vpy  = "narval_py_tmp_" + stem + ".py";

    {
        std::ofstream f(vpy);
        f << "from " << module << " import *\n";
    }

    ExternStmtNode tmp_node("Python", decls);
    tmp_node.source_file = vpy;
    backend_for("Python", vpy).emit(tmp_node, ctx);
    std::filesystem::remove(vpy);
}

} // namespace nv::ffi
