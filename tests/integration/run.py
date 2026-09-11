#!/usr/bin/env python3
"""Narval integration tests.

Each case is a small Narval program (source string) plus its expected stdout.
The case is written to a temp .nv file, compiled+run with the narval binary
(argv[1]) and stdout is compared byte-for-byte. Exit code 0 = all cases pass.

Run via ctest: add_test(NAME narval_integration COMMAND python3 run.py <narval>)
"""

import subprocess
import sys
import tempfile
import os

CASES = [
    {
        "name": "basic_arith_io",
        "source": (
            'write(1 + 2 * 3);\n'
            'write("hello");\n'
            'write(2.5 > 1.0);\n'
            'write(7 == 7);\n'
        ),
        "expected": "7\nhello\ntrue\ntrue\n",
    },
    {
        "name": "function_call",
        "source": (
            'def add(a: int, b: int): int {\n'
            '    return a + b;\n'
            '}\n'
            'def twice(v: int): int {\n'
            '    return add(v, v);\n'
            '}\n'
            'write(add(2, 3));\n'
            'write(twice(21));\n'
        ),
        "expected": "5\n42\n",
    },
    {
        "name": "control_flow",
        "source": (
            'x = 4;\n'
            'if (x > 2) {\n'
            '    write("big");\n'
            '} else {\n'
            '    write("small");\n'
            '}\n'
            'match x {\n'
            '    1 => write("one");\n'
            '    4 => write("four");\n'
            '    _ => write("other");\n'
            '}\n'
            'write(x if x == 4 else 0);\n'
        ),
        "expected": "big\nfour\n4\n",
    },
    {
        # Early-return inside def bodies (if-statement with returns in the
        # branches) and recursion — regressions for the CFG lowering of
        # statement-ifs (scf.if regions cannot hold func.return).
        "name": "function_control_flow",
        "source": (
            'def pick(n: int): int {\n'
            '    if (n <= 1) {\n'
            '        return 1;\n'
            '    }\n'
            '    return 0;\n'
            '}\n'
            'def fact(n: int): int {\n'
            '    if (n <= 1) {\n'
            '        return 1;\n'
            '    }\n'
            '    return n * fact(n - 1);\n'
            '}\n'
            'write(pick(1));\n'
            'write(pick(5));\n'
            'write(fact(1));\n'
            'write(fact(5));\n'
        ),
        "expected": "1\n0\n1\n120\n",
    },
    {
        # A zero-arg `def main` is invoked after top-level code (C-like entry
        # convention); explicit top-level calls are not duplicated.
        "name": "main_entry",
        "source": (
            'write("top");\n'
            'def main(): int {\n'
            '    write("main");\n'
            '    return 0;\n'
            '}\n'
        ),
        "expected": "top\nmain\n",
    },
    {
        # Loop-carried mutation across iterations (while with reassigned
        # variables) + break, regression for the carried-value CFG lowering.
        "name": "loop_mutation",
        "source": (
            "i = 0;\n"
            "soma = 0;\n"
            "while (true) {\n"
            "    if (i >= 5) {\n"
            "        break;\n"
            "    }\n"
            "    soma = soma + i;\n"
            "    i = i + 1;\n"
            "}\n"
            "write(soma);\n"
            "write(i);\n"
        ),
        "expected": "10\n5\n",
    },
    {
        # Closures are first-class: creation, calling through the handle,
        # captures, and higher-order passing into a def.
        "name": "closure_call",
        "source": (
            "def apply(f: (int): int, x: int): int {\n"
            "    return f(x);\n"
            "}\n"
            "def counter(start: int): int {\n"
            "    n = start;\n"
            "    f = |x: int|: int { return x + n; };\n"
            "    return f(100);\n"
            "}\n"
            "def main(): int {\n"
            "    write(apply(|x: int|: int { return x * 3; }, 7));\n"
            "    write(counter(5));\n"
            "    g = |a: int, b: int|: int { return a + b; };\n"
            "    write(g(20, 22));\n"
            "    return 0;\n"
            "}\n"
        ),
        "expected": "21\n105\n42\n",
    },
    {
        # for_range lowers to a CFG with carried values: range loop,
        # accumulator (loop-carried), break/continue, collection iteration.
        "name": "for_range",
        "source": (
            "for i in 0..3 {\n"
            "    write(i);\n"
            "}\n"
            "soma = 0;\n"
            "for i in 1..=5 {\n"
            "    soma = soma + i;\n"
            "}\n"
            "write(soma);\n"
            "for i in 0..10 {\n"
            "    if (i >= 3) {\n"
            "        break;\n"
            "    }\n"
            "    write(i);\n"
            "}\n"
            "for x in [10, 20] {\n"
            "    write(x);\n"
            "}\n"
        ),
        "expected": "0\n1\n2\n15\n0\n1\n2\n10\n20\n",
    },
    {
        # break/continue lower correctly (regression: they used to be yield
        # placeholders that never exited the loop).
        "name": "loop_break",
        "source": (
            'while (true) {\n'
            '    write("a");\n'
            '    if (1 == 1) {\n'
            '        break;\n'
            '    }\n'
            '    write("c");\n'
            '}\n'
            'write("fim");\n'
        ),
        "expected": "a\nfim\n",
    },
    {
        # Tensor.zeros/ones go through the MLIR tensor path
        # (tensor.empty + linalg.fill + narval.tensor_to_value boxing).
        "name": "tensor_mlir_path",
        "source": (
            'z = Tensor.zeros(2, 3);\n'
            'write(z.shape);\n'
            'write(z.tolist());\n'
            'write(z.item());\n'
            'o = Tensor.ones(1, 2);\n'
            'write(o.shape);\n'
            'write(o.item());\n'
        ),
        "expected": (
            "[2, 3]\n"
            "[[0.000000, 0.000000, 0.000000], "
            "[0.000000, 0.000000, 0.000000]]\n"
            "0.000000\n"
            "[1, 2]\n"
            "1.000000\n"
        ),
    },
    {
        "name": "comptime_const_arith",
        "source": (
            'comptime N = 1024;\n'
            'comptime A = 2 + 3 * 4;\n'
            'write(N);\n'
            'write(A);\n'
        ),
        "expected": "1024\n14\n",
    },
    {
        "name": "comptime_func_recursion",
        "source": (
            'comptime def fib(n: int): int {\n'
            '    if n <= 1 { return n; }\n'
            '    return fib(n - 1) + fib(n - 2);\n'
            '}\n'
            'comptime X = fib(10);\n'
            'write(X);\n'
        ),
        "expected": "55\n",
    },
    {
        "name": "comptime_if_removes_branch",
        "source": (
            'comptime PLATFORM = "linux";\n'
            'comptime if PLATFORM == "linux" {\n'
            '    write("linux-build");\n'
            '} else {\n'
            '    write("other-build");\n'
            '}\n'
        ),
        "expected": "linux-build\n",
    },
    {
        "name": "comptime_for_unroll",
        "source": (
            'comptime for i in 0..3 {\n'
            '    write(i);\n'
            '}\n'
        ),
        "expected": "0\n1\n2\n",
    },
    {
        "name": "comptime_block",
        "source": (
            'comptime {\n'
            '    V = 5 * 5;\n'
            '}\n'
            'write(V);\n'
        ),
        "expected": "25\n",
    },
    {
        "name": "comptime_inside_function",
        "source": (
            'def f(): int {\n'
            '    comptime K = 7;\n'
            '    return K + 1;\n'
            '}\n'
            'write(f());\n'
        ),
        "expected": "8\n",
    },
    {
        "name": "zig_inline_for",
        "source": (
            'inline for i in 0..3 {\n'
            '    write(i);\n'
            '}\n'
        ),
        "expected": "0\n1\n2\n",
    },
    {
        "name": "zig_comptime_expr_inline",
        "source": (
            'x = comptime 2 + 3 * 4;\n'
            'write(x);\n'
        ),
        "expected": "14\n",
    },
    {
        "name": "zig_builtin_reflection",
        "source": (
            'class User {\n'
            '    name: str;\n'
            '    age: int;\n'
            '}\n'
            'write(@typeName(User));\n'
            'write(@hasField(User, "age"));\n'
            'comptime for f in @fieldNames(User) {\n'
            '    write(f);\n'
            '}\n'
        ),
        "expected": "User\ntrue\nage\nname\n",
    },
    {
        "name": "zig_inline_while",
        "source": (
            'comptime k = 0;\n'
            'inline while k < 3 {\n'
            '    write(k);\n'
            '    k = k + 1;\n'
            '}\n'
        ),
        "expected": "0\n1\n2\n",
    },
    {
        "name": "class_methods",
        "source": (
            'class User {\n'
            '    mut name: str;\n'
            '    mut age: int;\n'
            '    public greet(): str {\n'
            '        return "hi " + self.name;\n'
            '    }\n'
            '    public describe(): str {\n'
            '        return self.name + "@" + str(self.age);\n'
            '    }\n'
            '}\n'
            'u = new User();\n'
            'u.name = "bob";\n'
            'u.age = 42;\n'
            'write(u.name);\n'
            'write(u.greet());\n'
            'write(u.describe());\n'
        ),
        "expected": "bob\nhi bob\nbob@42\n",
    },
    {
        "name": "derive_eq_debug_json",
        "source": (
            '@[derive(eq, debug, json)]\n'
            'class User {\n'
            '    name: str;\n'
            '    age: int;\n'
            '}\n'
            'a = new User();\n'
            'a.name = "bob";\n'
            'a.age = 42;\n'
            'b = new User();\n'
            'b.name = "bob";\n'
            'b.age = 42;\n'
            'c = new User();\n'
            'c.name = "bob";\n'
            'c.age = 7;\n'
            'write(a.__eq__(b));\n'
            'write(a.__eq__(c));\n'
            'write(a.__str__());\n'
            'write(a.to_json());\n'
        ),
        "expected": "true\nfalse\nUser { name: \"bob\", age: 42 }\n{\"name\": \"bob\", \"age\": 42}\n",
    },
    {
        "name": "comptime_params",
        "source": (
            'def pow2(comptime N: int): int {\n'
            '    return N * N;\n'
            '}\n'
            'comptime K = 9;\n'
            'write(pow2(8));\n'
            'write(pow2(K));\n'
        ),
        "expected": "64\n81\n",
    },
    {
        "name": "dsl_macro_verbatim_body",
        "source": (
            'comptime def sql!(src: str): str {\n'
            '    return "query(" + src + ")";\n'
            '}\n'
            'write(sql! { SELECT 1 });\n'
            'q = sql! { SELECT * FROM users };\n'
            'write(q);\n'
        ),
        "expected": "query( SELECT 1 )\nquery( SELECT * FROM users )\n",
    },
    {
        "name": "comptime_table",
        "source": (
            'comptime T = [1, 2, 3, 4];\n'
            'write(T[0]);\n'
            'write(T[3]);\n'
            'comptime S = "abc";\n'
            'write(S);\n'
        ),
        "expected": "1\n4\nabc\n",
    },
    {
        "name": "comptime_import_c",
        "source": (
            'comptime import_c("mymath.h", link: "m");\n'
            'write(abs(-7));\n'
            'write(pow(2.0, 10.0));\n'
            'write(strlen("hello"));\n'
            'write(atof("2.5"));\n'
        ),
        "files": {
            "mymath.h": "double pow(double base, double exp);\nint abs(int x);\nlong strlen(const char *s);\ndouble atof(const char *s);\nvoid srand(unsigned int seed);\n",
        },
        "expected": "7\n1024.000000\n5\n2.500000\n",
    },
    {
        "name": "tensor_broadcast",
        "source": (
            'a: Tensor<float, [1, 3]> = Tensor.ones(1, 3);\n'
            'b: Tensor<float, [2, 3]> = Tensor.ones(2, 3);\n'
            'c = a + b;\n'
            'write(c.shape);\n'
            'write(c.tolist());\n'
        ),
        "expected": "[2, 3]\n[[2.000000, 2.000000, 2.000000], [2.000000, 2.000000, 2.000000]]\n",
    },
    {
        "name": "tensor_broadcast_shape_error",
        "source": (
            'd: Tensor<float, [2, 3]> = Tensor.zeros(2, 3);\n'
            'e: Tensor<float, [2, 4]> = Tensor.zeros(2, 4);\n'
            'f = d + e;\n'
            'write(f.shape);\n'
        ),
        "expect_error": "not broadcastable",
    },
    {
        "name": "derive_hash_ord_and_len",
        "source": (
            '@[derive(eq, hash, ord)]\n'
            'class P {\n'
            '    x: int;\n'
            '    y: int;\n'
            '}\n'
            'a = new P();\n'
            'a.x = 1;\n'
            'a.y = 2;\n'
            'c = new P();\n'
            'c.x = 3;\n'
            'c.y = 0;\n'
            'write(a.__lt__(c));\n'
            'write(c.__lt__(a));\n'
            'write(a.__hash__());\n'
            'write(c.__hash__());\n'
            'write(len("hello"));\n'
        ),
        "expected": "true\nfalse\n33\n93\n5\n",
    },
    {
        "name": "comptime_target_builtins",
        "source": (
            'comptime ARCH = target.arch();\n'
            'comptime SIMD = target.simd();\n'
            'comptime if ARCH == "x86_64" { write("arch-known"); }\n'
            'comptime if ARCH == "aarch64" { write("arch-known"); }\n'
            'comptime if ARCH == "x86" { write("arch-known"); }\n'
            'comptime if ARCH == "arm" { write("arch-known"); }\n'
            'comptime if ARCH == "riscv64" { write("arch-known"); }\n'
            'comptime if ARCH == "unknown" { write("arch-known"); }\n'
            'comptime if SIMD == "avx512" { write("simd-known"); }\n'
            'comptime if SIMD == "avx2" { write("simd-known"); }\n'
            'comptime if SIMD == "avx" { write("simd-known"); }\n'
            'comptime if SIMD == "sse2" { write("simd-known"); }\n'
            'comptime if SIMD == "neon" { write("simd-known"); }\n'
            'comptime if SIMD == "scalar" { write("simd-known"); }\n'
        ),
        "expected": "arch-known\nsimd-known\n",
    },
    {
        "name": "autodiff_attribute",
        "source": (
            'comptime import_c("mymath2.h", link: "m");\n'
            '\n'
            '@[diff(x, "dloss_dx")]\n'
            'def loss(x: float): float {\n'
            '    return x * x + sin(x);\n'
            '}\n'
            '\n'
            'write(dloss_dx(0.0));\n'
            'write(dloss_dx(1.0));\n'
        ),
        "files": {
            "mymath2.h": "double sin(double x);\ndouble cos(double x);\ndouble exp(double x);\ndouble log(double x);\ndouble sqrt(double x);\n",
        },
        "expected": "1.000000\n2.540302\n",
    },
    {
        "name": "autodiff_unsupported_rule",
        "source": (
            '@[diff(x, "dx")]\n'
            'def h(x: float): float {\n'
            '    return unknown_fn(x) + 1;\n'
            '}\n'
        ),
        "expect_error": "no derivative rule for 'unknown_fn'",
    },
    {
        "name": "diagnostic_ce003_runtime_value",
        "source": (
            'n = 5;\n'
            'comptime for i in n..10 {\n'
            '    write(i);\n'
            '}\n'
        ),
        "expect_error": "error[CE003]",
    },
    {
        "name": "diagnostic_ce002_tensor_shape",
        "source": (
            'd: Tensor<float, [2, 3]> = Tensor.zeros(2, 3);\n'
            'e: Tensor<float, [2, 4]> = Tensor.zeros(2, 4);\n'
            'f = d + e;\n'
        ),
        "expect_error": "error[CE002]",
    },
    {
        "name": "diagnostic_ce001_bad_derive",
        "source": (
            '@[derive(nope)]\n'
            'class Q {\n'
            '    a: int;\n'
            '}\n'
        ),
        "expect_error": "error[CE001]",
    },
    {
        "name": "import_module_macros_and_comptime",
        "source": (
            'from "libmac.nv" import *;\n'
            'write(sql! { SELECT 1 });\n'
            'write(regex! { a+ });\n'
            'write(LIMIT);\n'
            'write(libval());\n'
        ),
        "files": {
            "libmac.nv": "comptime def sql!(src: str): str {\n    return \"SQL[\" + src + \"]\";\n}\ncomptime def regex!(src: str): str {\n    return \"RE[\" + src + \"]\";\n}\ncomptime LIMIT = 7;\ndef libval(): int {\n    return 42;\n}\n",
        },
        "expected": "SQL[ SELECT 1 ]\nRE[ a+ ]\n7\n42\n",
    },
    {
        "name": "string_compare_and_index",
        "source": (
            's = "hello";\n'
            'write(len(s));\n'
            'write(s == "hello");\n'
            'write(s != "x");\n'
            'write(s == "hellp");\n'
            'write(s[0]);\n'
            'write(s[4]);\n'
            'write(s[99]);\n'
            'write("abc" != "abc");\n'
        ),
        "expected": "5\ntrue\ntrue\nfalse\nh\no\n\nfalse\n",
    },
    {
        "name": "stdlib_macros",
        "source": (
            'from "macros.nv" import *;\n'
            'write(sql! { SELECT *   FROM   users });\n'
            'write(regex! { a(b|c)*[0-9] });\n'
            'write(route! { GET /users/:id });\n'
        ),
        "module_files": {
            "macros.nv": "stdlib/macros.nv",
        },
        "expected": "SELECT * FROM users\na(b|c)*[0-9]\nGET /users/:id\n",
    },
    {
        "name": "stdlib_macros_reject_bad_sql",
        "source": (
            'from "macros.nv" import *;\n'
            'write(sql! { SELCT x });\n'
        ),
        "module_files": {
            "macros.nv": "stdlib/macros.nv",
        },
        "expect_error": "@compileError: sql!: expected the query to start with",
    },
    {
        "name": "stdlib_macros_reject_unbalanced_regex",
        "source": (
            'from "macros.nv" import *;\n'
            'write(regex! { a(b });\n'
        ),
        "module_files": {
            "macros.nv": "stdlib/macros.nv",
        },
        "expect_error": "regex!: unbalanced '('",
    },
    {
        "name": "stdlib_strings",
        "source": (
            'from "strings.nv" import *;\n'
            'write(trim("  hi  "));\n'
            'write(starts_with("hello", "he"));\n'
            'write(starts_with("hello", "he!"));\n'
            'write(ends_with("hello", "lo"));\n'
            'write(index_of("hello", "ll"));\n'
            'write(index_of("hello", "z"));\n'
            'write(substr("hello", 1, 3));\n'
            'write(count_of("banana", "an"));\n'
            'parts = split("a,b,c", ",");\n'
            'write(len(parts));\n'
            'write(parts[0]);\n'
            'write(parts[2]);\n'
            'write(join(["x", "y", "z"], "-"));\n'
            'write(replace("a-b-c", "-", "+"));\n'
        ),
        "module_files": {
            "strings.nv": "stdlib/strings.nv",
        },
        "expected": "hi\ntrue\nfalse\ntrue\n2\n-1\nell\n2\n3\na\nc\nx-y-z\na+b+c\n",
    },
    {
        "name": "stdlib_strings_vector_push",
        "source": (
            'def build(n: int): vector {\n'
            '    v = [];\n'
            '    i = 0;\n'
            '    while i < n {\n'
            '        v.push(i);\n'
            '        i = i + 1;\n'
            '    }\n'
            '    return v;\n'
            '}\n'
            'r = build(3);\n'
            'write(len(r));\n'
            'write(r[0]);\n'
            'write(r[2]);\n'
        ),
        "expected": "3\n0\n2\n",
    },
    {
        "name": "type_ast_reflection",
        "source": (
            'comptime def poly(x: int): int {\n'
            '    y = x * x + 3;\n'
            '    return y;\n'
            '}\n'
            'write(type.ast(poly).name);\n'
            'write(type.ast(poly).is_macro);\n'
            'write(type.ast(poly).return_type);\n'
            'write(type.ast(poly).statement_count);\n'
            'comptime for n in type.ast(poly).param_names { write(n); }\n'
            'comptime for k in type.ast(poly).body_kinds { write(k); }\n'
            'comptime for op in type.ast(poly).expr_ops { write(op); }\n'
        ),
        "expected": "poly\nfalse\nint\n2\nx\nassignment\nreturn\n=\n+\n*\n",
    },
    {
        "name": "type_ast_rejects_runtime_function",
        "source": (
            'def plain(x: int): int {\n'
            '    return x;\n'
            '}\n'
            'write(type.ast(plain).name);\n'
        ),
        "expect_error": "only comptime functions can be inspected",
    },
    {
        "name": "comptime_guard",
        "source": (
            'comptime def first_stop!(s: str): str {\n'
            '    i = 0;\n'
            '    while i < len(s) && s[i] != "x" {\n'
            '        if s[i] == "Q" {\n'
            '            @compileError("unexpected Q in: " + s)\n'
            '        }\n'
            '        i = i + 1;\n'
            '    }\n'
            '    return str(i);\n'
            '}\n'
            'write(first_stop! { abcxdef });\n'
            'write("guard ok");\n'
        ),
        "expected": "4\nguard ok\n",
    },
    {
        "name": "stdlib_grammar_ok",
        "source": (
            'from "grammar.nv" import *;\n'
            '\n'
            'write(grammar! {\n'
            "    expr   = term (('+' | '-') term)*\n"
            "    term   = factor (('*' | '/') factor)*\n"
            "    factor = NUMBER | '(' expr ')'\n"
            '});\n'
        ),
        "module_files": {
            "grammar.nv": "stdlib/grammar.nv",
        },
        "expected": "grammar: 3 rule(s) [expr term factor], terminals ['+' '-' '*' '/' NUMBER '(' ')']\n",
    },
    {
        "name": "stdlib_grammar_rejects_undefined",
        "source": (
            'from "grammar.nv" import *;\n'
            '\n'
            'write(grammar! {\n'
            '    expr = term | missing\n'
            '});\n'
        ),
        "module_files": {
            "grammar.nv": "stdlib/grammar.nv",
        },
        "expect_error": "uses undefined nonterminal",
    },
    {
        "name": "stdlib_grammar_rejects_left_recursion",
        "source": (
            'from "grammar.nv" import *;\n'
            '\n'
            'write(grammar! {\n'
            "    expr = expr '+' term\n"
            '    term = NUMBER\n'
            '});\n'
        ),
        "module_files": {
            "grammar.nv": "stdlib/grammar.nv",
        },
        "expect_error": "is left recursive",
    },
    {
        "name": "stdlib_grammar_rejects_ll1_conflict",
        "source": (
            'from "grammar.nv" import *;\n'
            '\n'
            'write(grammar! {\n'
            '    expr = NUMBER | NUMBER\n'
            '});\n'
        ),
        "module_files": {
            "grammar.nv": "stdlib/grammar.nv",
        },
        "expect_error": "two alternatives starting with",
    },
    {
        "name": "derive_clone_and_from_json",
        "source": (
            '@[derive(debug, clone, from_json)]\n'
            'class User {\n'
            '    name: str;\n'
            '    age: int;\n'
            '    admin: bool;\n'
            '}\n'
            'a = new User();\n'
            'a.name = "bob";\n'
            'a.age = 42;\n'
            'a.admin = true;\n'
            'b = a.clone();\n'
            'write(b.name);\n'
            'write(b.age);\n'
            'c = a.clone();\n'
            'c.name = "ana";\n'
            'write(a.name == c.name);\n'
            'write(b.name == c.name);\n'
            'd = a.from_json("{\\"name\\": \\"zoe\\", \\"age\\": 7, \\"extra\\": {\\"n\\": 1}}");\n'
            'write(d.name);\n'
            'write(d.age);\n'
            'write(d.admin == false);\n'
            'write(d.name == b.name);\n'
        ),
        "expected": "bob\n42\nfalse\nfalse\nzoe\n7\ntrue\nfalse\n",
    },
    {
        "name": "derive_from_json_rejects_nested_type",
        "source": (
            '@[derive(from_json)]\n'
            'class Inner {\n'
            '    x: int;\n'
            '}\n'
            '@[derive(from_json)]\n'
            'class Outer {\n'
            '    inner: Inner;\n'
            '}\n'
            'write(1);\n'
        ),
        "expect_error": "has unsupported type",
    },
    {
        "name": "sqlite_open_exec_query",
        "source": (
            'from "sqlite.nv" import *;\n'
            '\n'
            'db = new Sqlite();\n'
            'write(db.open("/tmp/narval_it_sqlite.db") >= 0);\n'
            'write(db.exec("DROP TABLE IF EXISTS t"));\n'
            'write(db.exec("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT, score REAL)"));\n'
            'write(db.exec("INSERT INTO t (name, score) VALUES (\'bob\', 9.5)"));\n'
            'write(db.exec("INSERT INTO t (name, score) VALUES (\'ana\', 7)"));\n'
            'write(db.last_id());\n'
            'write(db.changes());\n'
            'write(db.query("SELECT name, score FROM t ORDER BY id"));\n'
            'write(db.cols());\n'
            'first = db.row(0);\n'
            'second = db.row(1);\n'
            'write(first[0]);\n'
            'write(second[1]);\n'
            'write(db.exec("SELECT * FROM nao_existe"));\n'
            'write(len(db.error()) > 0);\n'
            'write(db.close());\n'
        ),
                "module_files": {
            "sqlite.nv": "stdlib/sqlite.nv",
        },
"expected": "true\n0\n0\n0\n0\n2\n1\n2\n2\nbob\n7.0\n1\ntrue\n0\n",
    },
    {
        "name": "derive_openapi_schema",
        "source": (
            '@[derive(openapi)]\n'
            'class User {\n'
            '    name: str;\n'
            '    age: int;\n'
            '    admin: bool;\n'
            '}\n'
            'a = new User();\n'
            'write(a.schema());\n'
        ),
        "expected": "{\"type\": \"object\", \"properties\": {\"name\": {\"type\": \"string\"}, \"age\": {\"type\": \"integer\"}, \"admin\": {\"type\": \"boolean\"}}, \"required\": [\"name\", \"age\", \"admin\"]}\n",
    },
    {
        "name": "ffi_string_return",
        "source": (
            'comptime import_c("envprobe.h", link: "c");\n'
            'write(len(getenv("HOME")) > 0);\n'
        ),
        "files": {
            "envprobe.h": 'char *getenv(const char *name);\n',
        },
        "expected": "true\n",
    },
    {
        "name": "nested_module_import",
        "source": (
            'from "./nested_user.nv" import *;\n'
            'write(user_value());\n'
        ),
        "module_files": {
            "nested_dep.nv": "tests/fixtures/nested_dep.nv",
            "nested_user.nv": "tests/fixtures/nested_user.nv",
        },
        "expected": "8\n",
    },
    {
        "name": "assignment_escapes_nonconvertible_branch",
        "source": (
            'def cells(row: str): vector {\n'
            '    parts = [];\n'
            '    cell = "";\n'
            '    i = 0;\n'
            '    while i < len(row) {\n'
            '        c = row[i];\n'
            '        if c == "\\t" {\n'
            '            parts.push(cell);\n'
            '            cell = "";\n'
            '        } else {\n'
            '            cell = cell + c;\n'
            '        }\n'
            '        i = i + 1;\n'
            '    }\n'
            '    parts.push(cell);\n'
            '    return parts;\n'
            '}\n'
            '\n'
            'r = cells("a\\tb");\n'
            'write(len(r));\n'
            'first = r[0];\n'
            'second = r[1];\n'
            'write(first);\n'
            'write(second);\n'
        ),
        "expected": "2\na\nb\n",
    },
    {
        "name": "file_open_write_read",
        "source": (
            'from "file.nv" import *;\n'
            '\n'
            'f = new File();\n'
            'write(f.open("/tmp/narval_it_file.txt", "w") >= 0);\n'
            'write(f.write("um\\ndois\\n"));\n'
            'write(f.close());\n'
            'write(file_exists("/tmp/narval_it_file.txt"));\n'
            'g = new File();\n'
            'write(g.open("/tmp/narval_it_file.txt", "r") >= 0);\n'
            'write(g.read_line());\n'
            'write(g.read_line());\n'
            'write(g.close());\n'
            'write(file_remove("/tmp/narval_it_file.txt"));\n'
            'write(file_exists("/tmp/narval_it_file.txt"));\n'
        ),
        "expected": "true\n8\n0\n1\ntrue\num\ndois\n0\n0\n0\n",
        "module_files": {
            "file.nv": "stdlib/file.nv",
        },
    },
    {
        # Indexed store `a[i] = v` was a use-after-free and had no case in the
        # suite, which is exactly why it lived so long undetected.
        "name": "index_store",
        "source": (
            'v = [1, 2, 3];\n'
            'v[1] = 5;\n'
            'write(v[1]);\n'
            'write(v);\n'
        ),
        "expected": "5\n[1, 5, 3]\n",
    },
    {
        # Bare read(): the bridge always takes its optional prompt, so with no
        # argument it read a stale register and echoed the previous string.
        "name": "read_builtin",
        "source": (
            's = read();\n'
            'write(s);\n'
        ),
        "stdin": "hello\n",
        "expected": "hello\n",
    },
    {
        "name": "read_prompt",
        "source": (
            'n = read("Name? ");\n'
            'write("hi " + n);\n'
        ),
        "stdin": "bob\n",
        "expected": "Name? hi bob\n",
    },
    {
        "name": "option_result_values",
        "source": (
            'a = Some(5);\n'
            'b = Ok(7);\n'
            'c = Err("bad");\n'
            'd = None;\n'
            'write(a);\n'
            'write(b);\n'
            'write(c);\n'
            'write(d);\n'
        ),
        "expected": ("<object:Option::Some>\n<object:Result::Ok>\n"
                     "<object:Result::Err>\n<object:Option::None>\n"),
    },
    {
        # @[no_std] compiles a freestanding binary that is then run: collections,
        # conversions, len, index store and for-in over the syscall-only runtime.
        "name": "nostd_collections_conversions",
        "no_std": True,
        "source": (
            '@[no_std]\n'
            'def _start(): None {\n'
            '    v = [1, 2, 3];\n'
            '    v[1] = 9;\n'
            '    v.push(4);\n'
            '    write(v);\n'
            '    write(len(v));\n'
            '    write(int("42") + 1);\n'
            '    write(str(7));\n'
            '    write(float("2.5"));\n'
            '    write(bool(0));\n'
            '    write(char(65));\n'
            '    soma = 0;\n'
            '    for x in v { soma = soma + x; }\n'
            '    write(soma);\n'
            '}\n'
        ),
        "expected": "[1, 9, 3, 4]\n4\n43\n7\n2.500000\nfalse\nA\n17\n",
    },
    {
        "name": "nostd_option_result",
        "no_std": True,
        "source": (
            '@[no_std]\n'
            'def _start(): None {\n'
            '    a = Some(5);\n'
            '    b = Ok(7);\n'
            '    c = Err("bad");\n'
            '    d = None;\n'
            '    write(a);\n'
            '    write(b);\n'
            '    write(c);\n'
            '    write(d);\n'
            '}\n'
        ),
        "expected": ("<object:Option::Some>\n<object:Result::Ok>\n"
                     "<object:Result::Err>\n<object:Option::None>\n"),
    },
    {
        "name": "nostd_read",
        "no_std": True,
        "source": (
            '@[no_std]\n'
            'def _start(): None {\n'
            '    write("in:");\n'
            '    s = read();\n'
            '    write(s);\n'
            '}\n'
        ),
        "stdin": "world\n",
        "expected": "in:\nworld\n",
    },
    {
        "name": "nostd_exit_code",
        "no_std": True,
        "source": (
            '@[no_std]\n'
            'def _start(): None {\n'
            '    write("bye");\n'
            '    exit(7);\n'
            '}\n'
        ),
        "expected": "bye\n",
        "expected_rc": 7,
    },
    {
        # `extern "C:lib" { ... }` block form: known libraries go through the
        # nv_ffi_<name> bridges (before, the raw C symbol was declared and got a
        # boxed pointer where a double was expected — every call segfaulted).
        "name": "ffi_c_lib_block",
        "source": (
            'extern "C:math" {\n'
            '    def sqrt(x: float): float;\n'
            '    def pow(x: float, y: float): float;\n'
            '}\n'
            'extern "C:stdlib" {\n'
            '    def abs(x: int): int;\n'
            '}\n'
            'write(sqrt(16.0));\n'
            'write(pow(2.0, 10.0));\n'
            'write(abs(-7));\n'
        ),
        "expected": "4.000000\n1024.000000\n7\n",
    },
    {
        # @emit: a comptime macro generates Narval source, which is parsed and
        # spliced in as real defs (COMPTIME_SPEC 5.11 AST building).
        "name": "comptime_emit_def",
        "source": (
            'comptime def make_add!(src: str): str {\n'
            '    @emit("def gen_add(a: int, b: int): int { return a + b; }");\n'
            '    return "made";\n'
            '}\n'
            'make_add! { keep }\n'
            'write(gen_add(20, 22));\n'
        ),
        "expected": "42\n",
    },
    {
        # The emitted source is a normal comptime string, so it can be built with
        # loops and string concatenation.
        "name": "comptime_emit_computed",
        "source": (
            'comptime def gen_series!(src: str): str {\n'
            '    out = "";\n'
            '    for i in 0..3 {\n'
            '        if i > 0 { out = out + " "; }\n'
            '        out = out + "write(" + str(i * 10) + ");";\n'
            '    }\n'
            '    @emit(out);\n'
            '    return "series";\n'
            '}\n'
            'gen_series! { x }\n'
        ),
        "expected": "0\n10\n20\n",
    },
    {
        # grammar! now emits its recognizers (was validate-and-summarise only): the
        # generated parse_expr walks a token list and returns the position after the
        # match, or -1.
        "name": "stdlib_grammar_generates_parser",
        "source": (
            'from "grammar.nv" import *;\n'
            '\n'
            'write(grammar! {\n'
            "    expr   = term (('+' | '-') term)*\n"
            "    term   = factor (('*' | '/') factor)*\n"
            "    factor = NUMBER | '(' expr ')'\n"
            '});\n'
            't = ["NUMBER", "+", "NUMBER", "*", "NUMBER"];\n'
            'write(parse_expr(t, 0) == len(t));\n'
            'u = ["(", "NUMBER", "+", "NUMBER", ")"];\n'
            'write(parse_expr(u, 0));\n'
            'bad = ["NUMBER", "+", "*", "NUMBER"];\n'
            'write(parse_expr(bad, 0));\n'
        ),
        "module_files": {"grammar.nv": "stdlib/grammar.nv"},
        "expected": ("grammar: 3 rule(s) [expr term factor], terminals "
                     "['+' '-' '*' '/' NUMBER '(' ')']\n"
                     "true\n5\n1\n"),
    },
    {
        # The element of an untyped vector is itself unknown, and unknown values are
        # indexable — `rows[i][j]` used to be rejected ("requires array, vector,
        # string, map, or tuple, but got 't0'").
        "name": "nested_index",
        "source": (
            'rows = [["a", "b"], ["c", "d"]];\n'
            'write(rows[0][0]);\n'
            'write(rows[1][1]);\n'
            'cube = [[["deep"]]];\n'
            'write(cube[0][0][0]);\n'
            'def cell_at(m: vector, r: int, c: int): str {\n'
            '    return m[r][c];\n'
            '}\n'
            'write(cell_at(rows, 1, 0));\n'
        ),
        "expected": "a\nd\ndeep\nc\n",
    },
    {
        # Sqlite.rows() returns the result set as a vector of vectors now that a
        # nested index works.
        "name": "sqlite_rows_nested_index",
        "source": (
            'from "sqlite.nv" import *;\n'
            '\n'
            'db = new Sqlite();\n'
            'db.open("/tmp/narval_it_sqlite_rows.db");\n'
            'db.exec("DROP TABLE IF EXISTS t");\n'
            'db.exec("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT, score REAL)");\n'
            'db.exec("INSERT INTO t (name, score) VALUES (\'bob\', 9.5)");\n'
            'db.exec("INSERT INTO t (name, score) VALUES (\'ana\', 7)");\n'
            'n = db.query("SELECT name, score FROM t ORDER BY id");\n'
            'all = db.rows();\n'
            'write(n);\n'
            'write(all[0][0]);\n'
            'write(all[1][0]);\n'
            'write(all[1][1]);\n'
            'write(db.close());\n'
        ),
        "module_files": {"sqlite.nv": "stdlib/sqlite.nv"},
        "expected": "2\nbob\nana\n7.0\n0\n",
    },
    {
        # A value used across the blocks of a loop body AND in both arms of an if:
        # the shape the drops pass gets wrong unless the last-use reachability is
        # careful (same-block uses, and a block that reaches itself).
        "name": "loop_value_across_blocks",
        "source": (
            'i = 0;\n'
            'while i < 3 {\n'
            '    s = "x" + str(i);\n'
            '    if i == i {\n'
            '        write(s);\n'
            '    } else {\n'
            '        write("no");\n'
            '    }\n'
            '    i = i + 1;\n'
            '}\n'
            'write("fim");\n'
        ),
        "expected": "x0\nx1\nx2\nfim\n",
    },
]


def run_case(narval, case):
    name = case["name"]
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".nv", prefix=f"narval_it_{name}_", delete=False
    ) as f:
        f.write(case["source"])
        path = f.name
    # Extra files (headers for `comptime import_c`, ...) live next to the .nv,
    # which is also what import_c resolves a relative path against.
    extra = []
    for fname, content in case.get("files", {}).items():
        fpath = os.path.join(os.path.dirname(path), fname)
        with open(fpath, "w") as f:
            f.write(content)
        extra.append(fpath)
    # Module files taken from the repository (e.g. the bundled stdlib): copied
    # under the given name so the case imports the real file, not a duplicate.
    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    for fname, rel in case.get("module_files", {}).items():
        fpath = os.path.join(os.path.dirname(path), fname)
        with open(os.path.join(repo_root, rel)) as src, open(fpath, "w") as dst:
            dst.write(src.read())
        extra.append(fpath)
    try:
        if case.get("no_std"):
            # @[no_std] programs are not JIT-executed: build a freestanding
            # binary (-b) and run it, like a user would.
            tmpdir = os.path.dirname(path)
            stem = os.path.splitext(os.path.basename(path))[0]
            build = subprocess.run(
                [narval, "-b", path],
                capture_output=True, text=True, timeout=120, cwd=tmpdir,
            )
            if build.returncode != 0:
                print(f"[FAIL] {name}: no_std build failed\n{build.stderr[:800]}")
                return False
            extra.append(os.path.join(tmpdir, stem))
            proc = subprocess.run(
                [os.path.join(tmpdir, stem)],
                capture_output=True, text=True, timeout=60,
                input=case.get("stdin", ""),
            )
        else:
            proc = subprocess.run(
                [narval, path],
                capture_output=True, text=True, timeout=60,
                input=case.get("stdin", ""),
            )
    except subprocess.TimeoutExpired:
        print(f"[FAIL] {name}: timed out")
        return False
    finally:
        os.unlink(path)
        for fpath in extra:
            os.unlink(fpath)

    if case.get("expect_error"):
        # Compile-time diagnostic expected: the program must be rejected, and the
        # message must mention the given substring.
        if proc.returncode == 0:
            print(f"[FAIL] {name}: expected a compile-time error, but it compiled")
            return False
        needle = case["expect_error"]
        if needle not in (proc.stderr + proc.stdout):
            print(f"[FAIL] {name}: error did not mention {needle!r}\n{proc.stderr[:600]}")
            return False
        print(f"[PASS] {name}")
        return True

    expected_rc = case.get("expected_rc", 0)
    if proc.returncode != expected_rc:
        print(f"[FAIL] {name}: exit={proc.returncode} (want {expected_rc})\n"
              f"{proc.stderr[:800]}")
        return False
    if proc.stdout != case["expected"]:
        print(f"[FAIL] {name}: stdout mismatch\n--- expected ---\n"
              f"{case['expected']}--- got ---\n{proc.stdout}")
        return False
    print(f"[PASS] {name}")
    return True


def main():
    if len(sys.argv) < 2:
        print("usage: run.py <path-to-narval-binary>", file=sys.stderr)
        return 2
    narval = os.path.abspath(sys.argv[1])
    results = [run_case(narval, case) for case in CASES]
    ok = sum(1 for r in results if r)
    print(f"=== {ok}/{len(results)} integration tests passed ===")
    return 0 if ok == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
