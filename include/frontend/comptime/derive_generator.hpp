#pragma once
#include <string>
#include <vector>

class ClassStmtNode;
namespace nv { class Checker; }

namespace nv {

// Injects the methods requested by `@[derive(...)]` into a class node, before
// the class is type-checked. Supported derives: eq, debug, json, hash.
// Returns false and fills `error` for an unknown/unsupported derive name.
bool apply_derive(Checker* checker, ClassStmtNode* cls,
                  const std::vector<std::string>& derives, std::string& error);

} // namespace nv
