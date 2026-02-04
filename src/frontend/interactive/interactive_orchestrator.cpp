#include "frontend/interactive/interactive_orchestrator.hpp"

#include <stdexcept>

#include "frontend/checker/checker.hpp"
#include "frontend/interactive/execution_unit.hpp"
#include "frontend/interactive/fragment_parser.hpp"
#include "frontend/interactive/incremental_semantic_analyzer.hpp"
#include "frontend/interactive/ir_incremental_builder.hpp"
#include "frontend/interactive/jit_execution_engine.hpp"
#include "frontend/interactive/session_manager.hpp"
#include "frontend/interactive/symbol_collector.hpp"

#include <unordered_map>

namespace narval::frontend::interactive {

struct InteractiveOrchestrator::Engine {
    nv::Checker checker;
    FragmentParser parser;
    IncrementalSemanticAnalyzer analyzer;
    IrIncrementalBuilder ir_builder;
    JitExecutionEngine jit;
    SessionManager session;
    std::unordered_map<std::string, llvm::orc::ResourceTrackerSP> fragment_trackers;
    size_t unit_counter = 0;

    Engine() : analyzer(checker) {}
};

InteractiveOrchestrator::InteractiveOrchestrator() : engine_(std::make_unique<Engine>()) {}

InteractiveOrchestrator::~InteractiveOrchestrator() = default;

ExecutionResult InteractiveOrchestrator::execute(const IncrementalUnit& unit) {
    ExecuteOptions options;
    return execute(unit, options);
}

ExecutionResult InteractiveOrchestrator::execute(const IncrementalUnit& unit, const ExecuteOptions& options) {
    ExecutionResult out;

    const auto fail = [&](std::string msg) {
        out.ok = false;
        out.error = std::move(msg);
        return out;
    };

    auto program = engine_->parser.parse(unit.source, unit.virtual_filename);
    if (!program) {
        return fail("parse error");
    }

    auto usage = collect_symbol_usage(*program);

    std::shared_ptr<const ::Program> ast_ptr(program.release(), [](const ::Program* p) { delete p; });
    ExecutionUnit exec(
        unit.id,
        unit.virtual_filename,
        unit.source,
        unit.origin,
        0,
        ast_ptr,
        std::move(usage.defined),
        std::move(usage.used)
    );

    out.defined_symbols = exec.defined_symbols();
    out.used_symbols = exec.used_symbols();

    if (options.on_before_analysis) options.on_before_analysis(unit);

    auto sem = engine_->analyzer.analyze(exec, engine_->session);
    if (!sem.ok) {
        if (!sem.diagnostics.empty()) {
            return fail(sem.diagnostics.front().message);
        }
        return fail("semantic error");
    }

    const std::string unit_name = std::to_string(++engine_->unit_counter);

    IrBuildOptions ir_opts;
    ir_opts.auto_print_last_expr = options.auto_print_last_expr;

    if (options.invalidate_previous_fragment) {
        if (engine_->ir_builder.get_fragment(unit.id) && engine_->ir_builder.is_fragment_active(unit.id)) {
            auto inv = engine_->ir_builder.invalidate_fragment(unit.id);
            for (const auto& fid : inv.invalidated_fragments) {
                auto it = engine_->fragment_trackers.find(fid);
                if (it != engine_->fragment_trackers.end()) {
                    try {
                        engine_->jit.remove_module(it->second);
                    } catch (const std::exception& e) {
                        return fail(e.what());
                    }
                    engine_->fragment_trackers.erase(it);
                }
            }
        }
    }

    auto ir = engine_->ir_builder.build_fragment(
        const_cast<::Program&>(exec.ast()),
        engine_->checker,
        unit.id,
        unit_name,
        ir_opts
    );
    if (!ir.ok) {
        return fail(ir.error);
    }

    engine_->ir_builder.commit_fragment_interface(unit.id, exec.defined_symbols(), exec.used_symbols());

    try {
        auto tracker = engine_->jit.add_module(std::move(ir.module), std::move(ir.tsc));
        engine_->fragment_trackers[unit.id] = tracker;
        engine_->jit.execute_void_function(ir.entry_function);
    } catch (const std::exception& e) {
        return fail(e.what());
    }

    if (options.on_after_jit) options.on_after_jit(unit);

    out.ok = true;
    return out;
}

void InteractiveOrchestrator::reset() {
    engine_ = std::make_unique<Engine>();
}

SessionManager& InteractiveOrchestrator::session_manager() {
    return engine_->session;
}

} // namespace narval::frontend::interactive
