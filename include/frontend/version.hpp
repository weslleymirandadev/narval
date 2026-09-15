// The compiler's version, in one place.
//
// The build defines NARVAL_VERSION (CMakeLists.txt: the project version, overridable per configure
// so a release passes its git tag) and this header is the single spelling of the string the CLI,
// the REPL and the notebook print. A build without the definition would report an empty version,
// so it is a compile error instead.
#pragma once

#include <string>

#ifndef NARVAL_VERSION
#error "NARVAL_VERSION is not defined by the build (see the Narval and NarvalInteractive targets)"
#endif

namespace nv {

inline constexpr const char* kVersion = NARVAL_VERSION;

// "narval 1.0.0" — the help line and the REPL/notebook prompts start with this.
inline std::string version_prefix() {
    return std::string("narval ") + kVersion;
}

}  // namespace nv
