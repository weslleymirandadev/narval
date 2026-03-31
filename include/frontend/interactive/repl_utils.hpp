#pragma once

#include <string>
#include <memory>

#include "backend/runtime/prototypes.h"
#include <llvm/ExecutionEngine/JITSymbol.h>

namespace nv {

class Type;

namespace repl_utils {
	bool is_brace_balanced(const std::string& input);
	bool ends_with_operator(const std::string& input);
	bool needs_continuation(const std::string& input);

	std::string format_value(llvm::JITTargetAddress addr);
	std::string format_type(std::shared_ptr<Type> type);

	bool is_valid_identifier(const std::string& name);
	std::string sanitize_input(const std::string& input);
}

} // namespace nv
