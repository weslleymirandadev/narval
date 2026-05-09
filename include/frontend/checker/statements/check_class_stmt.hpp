#pragma once
#include <memory>

class Node;

namespace nv {
    class Type;
    class Checker;
    
    std::shared_ptr<Type> check_class_stmt(Checker* checker, Node* node);
}
