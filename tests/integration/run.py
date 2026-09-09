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
