#pragma once

#include <string>
#include <vector>

namespace nv {

enum class REPLCommand {
	HELP,
	QUIT,
	EXIT,
	RESET,
	VARS,
	HISTORY,
	LOAD,
	SAVE,
	CLEAR,
	NONE
};

class CommandParser {
public:
	static bool is_command(const std::string& input);
	static std::pair<REPLCommand, std::vector<std::string>> parse(const std::string& input);
};

} // namespace nv
