#pragma once
// The C libraries `from extern "C:lib"` may reach, as data. It lives in the frontend
// (nothing here needs LLVM/MLIR) because both sides use the same list: the checker
// registers the names a wildcard import brings in, the codegen registers the
// `nv_ffi_<name>` bridges for them.
#include <string>
#include <utility>
#include <vector>

namespace nv::ffi {

struct CLibFunc {
    std::string name;
    std::string return_type;
    std::vector<std::pair<std::string, std::string>> params; // (name, narval_type)
};

inline std::vector<CLibFunc> c_lib_registry(const std::string& libname) {
        static const std::vector<CLibFunc> math = {
            {"sin", "float", {{"x", "float"}}},
            {"cos", "float", {{"x", "float"}}},
            {"tan", "float", {{"x", "float"}}},
            {"asin", "float", {{"x", "float"}}},
            {"acos", "float", {{"x", "float"}}},
            {"atan", "float", {{"x", "float"}}},
            {"atan2", "float", {{"y", "float"}, {"x", "float"}}},
            {"sqrt", "float", {{"x", "float"}}},
            {"cbrt", "float", {{"x", "float"}}},
            {"pow", "float", {{"base", "float"}, {"exponent", "float"}}},
            {"exp", "float", {{"x", "float"}}},
            {"log", "float", {{"x", "float"}}},
            {"log10", "float", {{"x", "float"}}},
            {"log2", "float", {{"x", "float"}}},
            {"floor", "float", {{"x", "float"}}},
            {"ceil", "float", {{"x", "float"}}},
            {"round", "float", {{"x", "float"}}},
            {"fabs", "float", {{"x", "float"}}},
            {"fmod", "float", {{"x", "float"}, {"y", "float"}}},
            {"hypot", "float", {{"x", "float"}, {"y", "float"}}},
        };
        static const std::vector<CLibFunc> stdlib = {
            {"rand", "int", {}},
            {"srand", "None", {{"seed", "int"}}},
            {"abs", "int", {{"x", "int"}}},
            {"atoi", "int", {{"s", "string"}}},
            {"atof", "float", {{"s", "string"}}},
            {"exit", "None", {{"code", "int"}}},
            {"malloc", "int", {{"size", "int"}}},
            {"free", "None", {{"ptr", "int"}}},
        };
        static const std::vector<CLibFunc> string = {
            {"strlen", "int", {{"s", "string"}}},
            {"strcmp", "int", {{"a", "string"}, {"b", "string"}}},
            {"strncmp", "int", {{"a", "string"}, {"b", "string"}, {"n", "int"}}},
            {"strchr", "string", {{"s", "string"}, {"c", "int"}}},
            {"strstr", "string", {{"haystack", "string"}, {"needle", "string"}}},
        };
        static const std::vector<CLibFunc> stdio = {
            {"printf", "int", {{"fmt", "string"}}},
            {"puts", "int", {{"s", "string"}}},
            {"putchar", "int", {{"c", "int"}}},
            {"getchar", "int", {}},
        };
        static const std::vector<CLibFunc> time = {
            {"time", "int", {{"t", "int"}}},
            {"clock", "int", {}},
        };

        if (libname == "math" || libname == "m") return math;
        if (libname == "stdlib" || libname == "cstdlib") return stdlib;
        if (libname == "string" || libname == "cstring") return string;
        if (libname == "stdio" || libname == "cstdio") return stdio;
        if (libname == "time" || libname == "ctime") return time;
        return {};
}

} // namespace nv::ffi
