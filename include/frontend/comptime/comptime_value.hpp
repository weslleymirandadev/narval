#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace nv {

// A value produced while evaluating a `comptime` expression.
struct ComptimeValue {
    enum class Tag {
        Int, Float, Bool, Str,
        Array,   // arr_val
        Struct,  // struct_val (e.g. reflection field descriptor)
        Type,    // type_name (reflection)
        Void,
        None_,
    };

    Tag tag = Tag::None_;
    int64_t i_val = 0;
    double f_val = 0.0;
    bool b_val = false;
    std::string s_val;
    std::vector<ComptimeValue> arr_val;
    std::unordered_map<std::string, ComptimeValue> struct_val;
    std::string type_name;

    static ComptimeValue from_int(int64_t v);
    static ComptimeValue from_float(double v);
    static ComptimeValue from_bool(bool v);
    static ComptimeValue from_str(std::string v);
    static ComptimeValue from_array(std::vector<ComptimeValue> v);
    static ComptimeValue from_struct(std::unordered_map<std::string, ComptimeValue> v);
    static ComptimeValue from_type(std::string name);
    static ComptimeValue none();
    static ComptimeValue void_value();

    bool is_truthy() const;
    bool is_numeric() const;
    std::string to_string() const;
};

} // namespace nv
