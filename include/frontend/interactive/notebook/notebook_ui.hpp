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

private:
    static const std::string PROMPT;
    static const std::string MULTILINE_PROMPT;
};

} // namespace nv
