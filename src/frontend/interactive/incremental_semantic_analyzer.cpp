#include "frontend/interactive/incremental_semantic_analyzer.hpp"

#include "frontend/checker/checker.hpp"

#include "frontend/interactive/execution_unit.hpp"
#include "frontend/interactive/session_manager.hpp"

namespace narval::frontend::interactive {

IncrementalSemanticAnalyzer::IncrementalSemanticAnalyzer(nv::Checker& checker) : checker_(checker) {}

IncrementalAnalysisResult IncrementalSemanticAnalyzer::analyze(const ExecutionUnit& unit, SessionManager& session) {
    IncrementalAnalysisResult out;

    const std::string vfile = unit.virtual_filename();
    checker_.set_source_file(vfile);

    // 1) Guard: invalidated symbols become unavailable (not a cascaded error).
    {
        std::vector<std::string> invalid_used;
        for (const auto& u : unit.used_symbols()) {
            if (session.symbols().has(u) && !session.is_symbol_valid(u)) {
                invalid_used.push_back(u);
            }
        }
        if (!invalid_used.empty()) {
            IncrementalDiagnostic d;
            d.severity = DiagnosticSeverity::Error;
            d.virtual_filename = vfile;
            d.message = "invalidated symbol(s) used: ";
            for (size_t i = 0; i < invalid_used.size(); ++i) {
                if (i) d.message += ", ";
                d.message += invalid_used[i];
            }
            out.diagnostics.push_back(std::move(d));
            out.ok = false;
            return out;
        }
    }

    // 2) Redefinition diagnostics (warning-only, not a hard error).
    for (const auto& def : unit.defined_symbols()) {
        if (session.symbols().has(def) && session.is_symbol_valid(def)) {
            IncrementalDiagnostic d;
            d.severity = DiagnosticSeverity::Warning;
            d.virtual_filename = vfile;
            d.message = "redefinition of symbol: " + def;
            out.diagnostics.push_back(std::move(d));
        }
    }

    // 3) Semantic checking for this fragment only.
    // The checker carries the session scope; we analyze only the unit AST.
    checker_.check_node(const_cast<::Program*>(&unit.ast()));
    if (checker_.err) {
        IncrementalDiagnostic d;
        d.severity = DiagnosticSeverity::Error;
        d.virtual_filename = vfile;
        d.message = "semantic error";
        out.diagnostics.push_back(std::move(d));
        out.ok = false;
        return out;
    }

    // 4) Extract inferred types for defined symbols from checker scope.
    for (const auto& def : unit.defined_symbols()) {
        try {
            auto& ty = checker_.scope->get_key(def);
            out.defined_symbol_types.emplace(def, ty);
        } catch (...) {
            // Leave missing types absent.
        }
    }

    // 5) Commit to session (symbols + deps + origins + types).
    session.commit_unit(unit.to_incremental_unit(), out.defined_symbol_types);

    out.ok = true;
    return out;
}

} // namespace narval::frontend::interactive
