#pragma once
#include <memory>

class Node;

namespace nv {
    struct Type;
    class Checker;

    std::shared_ptr<Type> check_enum_stmt(Checker* checker, Node* node);
}
