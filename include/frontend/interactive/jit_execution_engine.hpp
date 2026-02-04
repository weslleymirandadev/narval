#pragma once

#include <memory>
#include <string>

#include <llvm/ExecutionEngine/Orc/Core.h>

namespace llvm {
class Module;
}

namespace llvm::orc {
class LLJIT;
class ThreadSafeContext;
}

namespace narval::frontend::interactive {

class JitExecutionEngine {
public:
    JitExecutionEngine();
    ~JitExecutionEngine();

    llvm::orc::ResourceTrackerSP add_module(
        std::unique_ptr<llvm::Module> module,
        std::unique_ptr<llvm::orc::ThreadSafeContext> tsc
    );

    void remove_module(const llvm::orc::ResourceTrackerSP& tracker);

    void execute_void_function(const std::string& name);

private:
    std::unique_ptr<llvm::orc::LLJIT> jit_;
};

} // namespace narval::frontend::interactive
