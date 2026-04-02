#pragma once

#include <string>
#include <vector>

namespace nv {

struct REPLConfig;

class InputProcessor {
public:
    explicit InputProcessor(const REPLConfig& config);
    ~InputProcessor() = default;

    std::string read_input(bool in_multiline = false);
    bool is_complete_expression(const std::string& input);
    std::string preprocess_input(const std::string& input);
    bool should_auto_print(const std::string& input);

private:
    const REPLConfig& config;
};

} // namespace nv
