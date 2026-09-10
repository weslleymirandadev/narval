#pragma once
#include <memory>
#include <string>

class FunctionStmtNode;
class Node;

namespace nv {

// `@[diff(x, "dfdx")]` on a function: symbolically differentiate the function's
// return expression with respect to the named parameter at compile time and
// build the derived function (same signature, new name), which the checker
// injects right after the annotated function — so it is checked and compiled
// like any other function.
//
// Supported nodes: numeric literals, identifiers, unary minus, + - * / and
// calls to sin, cos, tan, exp, log, sqrt, abs (chain rule). Anything else is
// refused with an explicit error rather than silently emitting a wrong
// derivative. The result is simplified (constant folding, 0/1 identities and
// `a + a -> 2*a`) so `x*x + sin(x)` yields `2*x + cos(x)`.
//
// Returns nullptr and fills `error` when the differentiation is not possible.
std::unique_ptr<Node> make_derivative(FunctionStmtNode* fn, const std::string& var,
                                      const std::string& new_name, std::string& error);

} // namespace nv
