#pragma once

#include <functional>
#include <memory>
#include <string>

#include "frontend/interactive/interactive_session.hpp"

namespace nv {
class Checker;
}

namespace narval::frontend::interactive {

class SessionManager;
class IncrementalSemanticAnalyzer;
class IrIncrementalBuilder;
class JitExecutionEngine;

class InteractiveOrchestrator {
public:
    struct ExecuteOptions {
        bool auto_print_last_expr = true;
        bool invalidate_previous_fragment = true;

        std::function<void(const IncrementalUnit&)> on_before_analysis;
        std::function<void(const IncrementalUnit&)> on_after_jit;
    };

    InteractiveOrchestrator();
    ~InteractiveOrchestrator();

    ExecutionResult execute(const IncrementalUnit& unit);

    ExecutionResult execute(const IncrementalUnit& unit, const ExecuteOptions& options);

    void reset();

    SessionManager& session_manager();

private:
    struct Engine;
    std::unique_ptr<Engine> engine_;
};

} // namespace narval::frontend::interactive
