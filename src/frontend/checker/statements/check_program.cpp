#include "frontend/checker/statements/check_program.hpp"
#include "frontend/ast/ast.hpp"
#include <stdexcept>

std::shared_ptr<rph::Type>& check_program_stmt(rph::Checker* ch, Node* node) {
    auto* program = static_cast<Program*>(node);

    for (auto& el : program->body) {
        try {
            ch->check_node(el.get());
        } catch (std::runtime_error e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }

    return ch->getty("void");
}