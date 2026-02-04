#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "frontend/ast/program.hpp"

namespace nv {
class Checker;
class Type;
}

namespace narval::frontend::interactive {

class ExecutionUnit;
class SessionManager;

enum class DiagnosticSeverity {
    Error,
    Warning,
};

struct IncrementalDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string message;
    std::string virtual_filename;
};

struct IncrementalAnalysisResult {
    bool ok = false;
    std::vector<IncrementalDiagnostic> diagnostics;

    // When ok=true, contains inferred types for the symbols defined in the unit.
    std::unordered_map<std::string, std::shared_ptr<nv::Type>> defined_symbol_types;
};

class IncrementalSemanticAnalyzer {
public:
    explicit IncrementalSemanticAnalyzer(nv::Checker& checker);

    // Analyzes semantics for a single unit and integrates with the session.
    // This must be incremental: it uses existing checker/session state and
    // does not reanalyze the whole session.
    IncrementalAnalysisResult analyze(const ExecutionUnit& unit, SessionManager& session);

private:
    nv::Checker& checker_;
};

} // namespace narval::frontend::interactive
