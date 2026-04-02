#pragma once

#include <string>
#include <vector>
#include "frontend/interactive/command_parser.hpp"

namespace nv {

struct REPLConfig;
class REPLState;

class CommandHandler {
public:
    explicit CommandHandler(REPLState* state, const REPLConfig& config);
    ~CommandHandler() = default;

    bool execute_command(REPLCommand cmd, const std::vector<std::string>& args);
    void show_help();
    void show_variables();
    void show_history();
    bool load_file(const std::string& filename);
    bool save_history(const std::string& filename);
    void reset_state();

private:
    REPLState* state;
    const REPLConfig& config;
};

} // namespace nv
