#pragma once
#include <string>
#include <vector>

class ClassStmtNode;
namespace nv { class Checker; class ComptimeEvaluator; }

namespace nv {

// Injects the methods requested by `@[derive(...)]` into a class node, before
// the class is type-checked. Builtin derives: eq, debug, json, hash, ord, clone,
// from_json, openapi.
//
// Any other name is a USER derive and is looked up as a `comptime def derive_<name>`
// in the module, registered on `ct` by the caller (see register_user_derives): it
// receives the class name and one "name: type" per field, and returns the class
// members it wants as Narval source, which is parsed and spliced in.
// Returns false and fills `error` for an unknown derive or a failing one.
bool apply_derive(Checker* checker, ClassStmtNode* cls,
                  const std::vector<std::string>& derives, std::string& error,
                  ComptimeEvaluator* ct = nullptr);

} // namespace nv
