#include "frontend/checker/builtins.hpp"
#include "frontend/checker/checker.hpp"
#include <vector>
#include <algorithm>

namespace nv {
    
    // Lista de funções builtin disponíveis
    const std::vector<BuiltinFunction> BUILTIN_FUNCTIONS = {
        // write: aceita 1 argumento de qualquer tipo -> None (como print() do Python)
        BuiltinFunction("write", {}, std::make_shared<None>(), false, true, 1, 1),
        
        // read: aceita 0 ou 1 argumento (prompt opcional), retorna string
        BuiltinFunction("read", {}, std::make_shared<String>(), false, true, 0, 1),
        // Threads: spawn returns an id into the runtime's table of live threads and wait
        // takes it back. NOT named `join`: the stdlib already has join(parts, sep) for
        // strings, and builtin names are global — a builtin shadows a library function of
        // the same name, so the library started failing with an arity error.
        // The value wait brings back is whatever the body returned — dynamic, so the
        // checker sees it as int until the language has an "any" type.
        BuiltinFunction("spawn", {}, std::make_shared<Int>(), false, true, 1, 1),
        BuiltinFunction("wait",  {}, std::make_shared<Int>(), false, true, 1, 1),
        // Channels: chan() is an id, send puts a value in, recv takes the oldest one and
        // blocks while empty. The value that comes out is dynamic (whatever was sent),
        // so the checker sees it as int until the language has an "any" type.
        BuiltinFunction("chan", {}, std::make_shared<Int>(),  false, true, 0, 0),
        BuiltinFunction("send", {}, std::make_shared<None>(), false, true, 2, 2),
        BuiltinFunction("recv", {}, std::make_shared<Int>(),  false, true, 1, 1),
        
        // exit: encerra o processo com o código dado (int) -> None
        BuiltinFunction("exit", {std::make_shared<Int>()}, std::make_shared<None>(), false, false, 1, 1),
        
        // Funções de conversão de tipo (estilo Python)
        // str: aceita 1 argumento de qualquer tipo, retorna string
        BuiltinFunction("str", {}, std::make_shared<String>(), false, true, 1, 1),

        // json_field: valor de um campo de topo de um objeto JSON (string
        // desescapada, numero como texto, true/false como bool, "" se faltar).
        BuiltinFunction("json_field", {}, std::make_shared<String>(), false, true, 2, 3),

        // SQLite: primitivas do shim do runtime. A API amigavel fica em
        // stdlib/sqlite.nv (libsqlite3 e carregada em runtime via dlopen).
        BuiltinFunction("nv_sqlite_open",    {}, std::make_shared<Int>(),    false, true, 1, 1),
        BuiltinFunction("nv_sqlite_close",   {}, std::make_shared<Int>(),    false, true, 1, 1),
        BuiltinFunction("nv_sqlite_exec",    {}, std::make_shared<Int>(),    false, true, 2, 2),
        BuiltinFunction("nv_sqlite_query",   {}, std::make_shared<String>(), false, true, 2, 2),
        BuiltinFunction("nv_sqlite_error",   {}, std::make_shared<String>(), false, true, 1, 1),
        BuiltinFunction("nv_sqlite_last_id", {}, std::make_shared<Int>(),    false, true, 1, 1),
        BuiltinFunction("nv_sqlite_changes", {}, std::make_shared<Int>(),    false, true, 1, 1),
        BuiltinFunction("nv_sqlite_query_run", {}, std::make_shared<Int>(),    false, true, 2, 2),
        BuiltinFunction("nv_sqlite_col_count", {}, std::make_shared<Int>(),    false, true, 1, 1),
        BuiltinFunction("nv_sqlite_cell",      {}, std::make_shared<String>(), false, true, 3, 3),
        BuiltinFunction("nv_sqlite_col_name",  {}, std::make_shared<String>(), false, true, 2, 2),

        // Arquivos: primitivas do shim do runtime. A API amigavel e a classe File
        // em stdlib/file.nv.
        BuiltinFunction("nv_file_open",      {}, std::make_shared<Int>(),    false, true, 2, 2),
        BuiltinFunction("nv_file_close",     {}, std::make_shared<Int>(),    false, true, 1, 1),
        BuiltinFunction("nv_file_read",      {}, std::make_shared<String>(), false, true, 1, 1),
        BuiltinFunction("nv_file_read_line", {}, std::make_shared<String>(), false, true, 1, 1),
        BuiltinFunction("nv_file_write",     {}, std::make_shared<Int>(),    false, true, 2, 2),
        BuiltinFunction("nv_file_exists",    {}, std::make_shared<Int>(),    false, true, 1, 1),
        BuiltinFunction("nv_file_remove",    {}, std::make_shared<Int>(),    false, true, 1, 1),        // len(x) — length of a string/vector/map; boxed int at runtime.
        BuiltinFunction("len", {}, std::make_shared<Int>(), false, true, 1, 1),
        
        // int: aceita 1 argumento de qualquer tipo, retorna int
        BuiltinFunction("int", {}, std::make_shared<Int>(), false, true, 1, 1),
        // The numeric widths as conversions: the spec writes these spellings, so they must
        // be usable on a value, not only as a tensor element type. The width is enforced by
        // the type (int32 and int64 do not unify); the storage is the same 64-bit box, so
        // this is a compile-time discipline, not a narrowing conversion.
        BuiltinFunction("int64", {}, std::make_shared<Int>(64), false, true, 1, 1),
        BuiltinFunction("int32", {}, std::make_shared<Int>(32), false, true, 1, 1),

        // char: tipo primitivo de 1 byte; conversão explícita vira char
        BuiltinFunction("char", {}, std::make_shared<Char>(), false, true, 1, 1),
        
        // float: aceita 1 argumento de qualquer tipo, retorna float
        BuiltinFunction("float", {}, std::make_shared<Float>(), false, true, 1, 1),
        BuiltinFunction("float64", {}, std::make_shared<Float>(64), false, true, 1, 1),
        BuiltinFunction("float32", {}, std::make_shared<Float>(32), false, true, 1, 1),
        
        // bool: aceita 1 argumento de qualquer tipo, retorna bool
        BuiltinFunction("bool", {}, std::make_shared<Boolean>(), false, true, 1, 1),

        // GPU builtins — usable inside @[gpu] kernel functions
        // thread_idx("x"|"y"|"z") → int  (gpu.thread_id)
        BuiltinFunction("thread_idx", {std::make_shared<String>()}, std::make_shared<Int>(), false, false, 1, 1),
        // block_idx("x"|"y"|"z") → int   (gpu.block_id)
        BuiltinFunction("block_idx",  {std::make_shared<String>()}, std::make_shared<Int>(), false, false, 1, 1),
        // block_dim("x"|"y"|"z") → int   (gpu.block_dim)
        BuiltinFunction("block_dim",  {std::make_shared<String>()}, std::make_shared<Int>(), false, false, 1, 1),
        // grid_dim("x"|"y"|"z") → int    (gpu.grid_dim)
        BuiltinFunction("grid_dim",   {std::make_shared<String>()}, std::make_shared<Int>(), false, false, 1, 1),
        // gpu_launch(kernel, grid_x, grid_y, grid_z, block_x, block_y, block_z, args...)
        BuiltinFunction("gpu_launch", {}, std::make_shared<None>(), false, true, 7, 0),

        // Option/Result constructors
        BuiltinFunction("Some",   {}, std::make_shared<None>(), false, true, 1, 1),
        BuiltinFunction("Ok",     {}, std::make_shared<None>(), false, true, 1, 1),
        BuiltinFunction("Err",    {}, std::make_shared<None>(), false, true, 1, 1),

        // tensor_zeros(d0, d1, ...) / tensor_ones(d0, d1, ...)
        // Return type: Tensor<float, [-1]> (dynamic; user annotates the real shape)
        BuiltinFunction("tensor_zeros", {},
            std::make_shared<TensorType>(std::make_shared<Float>(), std::vector<int64_t>{-1}),
            false, true, 0, 0),
        BuiltinFunction("tensor_ones", {},
            std::make_shared<TensorType>(std::make_shared<Float>(), std::vector<int64_t>{-1}),
            false, true, 0, 0),
    };
    
    // Variáveis globais builtin (não são funções, mas objetos especiais)
    void register_builtin_variables(Checker& checker) {
        // json: objeto especial para operações JSON
        // Criamos um tipo específico para json com método parse
        // Usamos Map como tipo genérico para JSON objetos, Vector para arrays
        auto json_type = std::make_shared<Map>(checker.gettyptr("str"), checker.gettyptr("str"));
        json_type->init_prototype();  // Inicializar o prototype
        
        // Adicionar métodos manualmente ao prototype do json
        if (json_type->prototype) {
            // Método parse: (string) -> Map (para arquivo)
            auto parse_func_type = std::make_shared<Function>(
                std::vector<std::shared_ptr<Type>>{checker.gettyptr("str")},
                json_type
            );
            json_type->prototype->put_key("parse", parse_func_type, true);
            
            // Método parseString: (string) -> Map (para string JSON)
            auto parse_string_func_type = std::make_shared<Function>(
                std::vector<std::shared_ptr<Type>>{checker.gettyptr("str")},
                json_type
            );
            json_type->prototype->put_key("parseString", parse_string_func_type, true);
            
            // Método dump: (Map, string) -> None
            auto dump_func_type = std::make_shared<Function>(
                std::vector<std::shared_ptr<Type>>{json_type, checker.gettyptr("str")},
                checker.gettyptr("None")
            );
            json_type->prototype->put_key("dump", dump_func_type, true);
            
            // Método stringify: (Map) -> string
            auto stringify_func_type = std::make_shared<Function>(
                std::vector<std::shared_ptr<Type>>{json_type},
                checker.gettyptr("str")
            );
            json_type->prototype->put_key("stringify", stringify_func_type, true);
        }
        
        // Registrar o tipo json no scope
        checker.scope->put_key("json", json_type, true);

    }
    
    bool builtin_accepts_args(const BuiltinFunction& builtin, size_t arg_count) {
        if (builtin.accepts_varargs) {
            return arg_count >= builtin.min_args && 
                   (builtin.max_args == 0 || arg_count <= builtin.max_args);
        }
        return arg_count == builtin.param_types.size();
    }
    
    void register_builtins(Checker& checker) {
        // Registrar funções builtin
        for (const auto& builtin : BUILTIN_FUNCTIONS) {
            std::shared_ptr<Type> func_type;
            
            if (builtin.is_polymorphic) {
                // Criar tipo polimórfico
                // Para write: aceita 0 ou 1 argumento de qualquer tipo
                // Criamos uma variável de tipo para o parâmetro opcional
                auto param_type = checker.unify_ctx.new_type_var();
                std::vector<std::shared_ptr<Type>> param_types = {param_type};
                
                func_type = std::make_shared<Function>(param_types, builtin.return_type);
                
                // Generalizar tipo (criar tipo polimórfico)
                // Coletar variáveis livres (apenas a variável do parâmetro)
                std::unordered_set<int> free_vars;
                param_type->collect_free_vars(free_vars);
                func_type = checker.unify_ctx.generalize(func_type, free_vars);
            } else {
                // Tipo não polimórfico - usar tipos especificados
                func_type = std::make_shared<Function>(builtin.param_types, builtin.return_type);
            }
            
            // Registrar no escopo global como constante
            checker.scope->put_key(builtin.name, func_type, true);
        }
        
        // Registrar variáveis globais builtin
        register_builtin_variables(checker);
    }
}
