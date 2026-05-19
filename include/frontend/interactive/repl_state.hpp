#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <optional>

// LLVM
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/NoFolder.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/InitLLVM.h>

// Narval deps
#include "frontend/checker/checker.hpp"
#include "backend/runtime/prototypes.h"

namespace nv {

struct REPLConfig;

#ifndef NARVAL_SOURCE_DIR
#define NARVAL_SOURCE_DIR "/home/bacal/projects/cpp/narval"
#endif

struct REPLState {
	std::unique_ptr<llvm::LLVMContext> llvm_context;
	std::unique_ptr<llvm::Module> module;
	std::unique_ptr<llvm::IRBuilder<llvm::NoFolder>> builder;
	std::unique_ptr<Checker> checker;
	std::unique_ptr<llvm::orc::LLJIT> jit;
	std::unordered_map<std::string, llvm::JITTargetAddress> symbols;
	std::vector<std::string> history;
	std::string last_result_var = "_";
	int result_counter = 0;
	std::unordered_set<std::string> repl_global_names;
	std::unordered_map<std::string, Value> repl_var_values;
	std::unordered_set<std::string> repl_globals_added;
	std::unordered_map<std::string, std::string> source_cache;
	
	// Reference to config for other modules
	const REPLConfig* config = nullptr;

	REPLState();
	~REPLState();

	bool initialize();
	void reset();
	void register_runtime_functions();
	void load_lib_modules();
	bool add_symbol(const std::string& name, llvm::JITTargetAddress addr);
	std::optional<llvm::JITTargetAddress> get_symbol(const std::string& name);
};

} // namespace nv
