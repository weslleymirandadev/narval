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
        "expected": "User\n-1\nage\nname\n",
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
]


def run_case(narval, case):
    name = case["name"]
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".nv", prefix=f"narval_it_{name}_", delete=False
    ) as f:
        f.write(case["source"])
        path = f.name
    try:
        proc = subprocess.run(
            [narval, path],
            capture_output=True,
            text=True,
            timeout=60,
        )
    except subprocess.TimeoutExpired:
        print(f"[FAIL] {name}: timed out")
        return False
    finally:
        os.unlink(path)

    if proc.returncode != 0:
        print(f"[FAIL] {name}: exit={proc.returncode}\n{proc.stderr[:800]}")
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
