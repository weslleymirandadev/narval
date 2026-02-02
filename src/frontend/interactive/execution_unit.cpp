#include "frontend/interactive/execution_unit.hpp"

namespace nv {
namespace interactive {

ExecutionUnit::ExecutionUnit(ExecutionUnitId id,
                             std::unique_ptr<Node> ast,
                             const std::string& source_name)
    : unit_id(id), ast(std::move(ast)), source_name(source_name), valid(true) {
}

} // namespace interactive
} // namespace nv
