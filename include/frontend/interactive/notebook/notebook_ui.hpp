#pragma once

#include <string>

namespace nv {

class NotebookUI {
public:
    NotebookUI() = default;
    ~NotebookUI() = default;

    std::string read_multiline();
    void show_help();
    void show_prompt();
    void show_input_prompt(int execution_count);
    void show_continuation_prompt(int execution_count);
    std::string read_input_line(int execution_count);
    std::string read_continuation_line(int execution_count);

private:
    static const std::string PROMPT;
    static const std::string MULTILINE_PROMPT;
};

} // namespace nv
