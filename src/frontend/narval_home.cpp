// Narval's home directory — the assets a compiler carries inside itself and needs on disk.
//
// Linking a program needs the runtime object, and compiling one needs the standard library
// modules. Both travel EMBEDDED in the narval binary (cmake/NarvalEmbedBlob.cmake), so a
// release is a single file: the first run writes them into a per-user directory and every
// later run reuses it (write_if_stale only rewrites a file whose size does not match the
// embedded copy). Without this, an installed compiler only worked when a stdlib/ directory
// sat next to it and when runtime.o was in the build tree or in NARVAL_HOME.
#include "frontend/embedded_assets.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace narval {
namespace {

// The version the binary was built as (CMake, NARVAL_VERSION): part of the cache path, so a
// new compiler never links the previous one's runtime or reads its stdlib.
#ifndef NARVAL_VERSION
#define NARVAL_VERSION "0.1.0"
#endif
constexpr const char* kVersion = NARVAL_VERSION;

void export_env(const char* key, const std::string& value) {
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    setenv(key, value.c_str(), 1);
#endif
}

// The embedded copy is the truth: the file is written when it is missing or when its bytes are
// not the embedded ones (an older compiler's asset, a short write). Comparing the content, not
// only the size, is what makes a rebuilt runtime safe here — a merged runtime.o can come back
// with the same size and different bytes.
bool write_if_stale(const std::filesystem::path& path,
                    const unsigned char* data, unsigned int len) {
    if (!data || len == 0) return false;
    std::error_code ec;
    if (std::filesystem::file_size(path, ec) == len && !ec) {
        std::ifstream in(path, std::ios::binary);
        std::vector<unsigned char> current(len);
        if (in.read(reinterpret_cast<char*>(current.data()), len) &&
            in.gcount() == static_cast<std::streamsize>(len) &&
            std::memcmp(current.data(), data, len) == 0)
            return true;
    }

    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(data), len);
    out.close();
    if (!out) return false;
    // Verify the size: a short write (full disk) must not leave an asset that looks usable.
    std::error_code check;
    return std::filesystem::file_size(path, check) == len && !check;
}

// $NARVAL_HOME when the caller set one (used as given — that is what the environment
// variable has always meant), otherwise the per-user cache directory.
std::vector<std::filesystem::path> home_candidates() {
    std::vector<std::filesystem::path> candidates;
    if (const char* env = std::getenv("NARVAL_HOME"); env && *env) {
        candidates.emplace_back(env);
        return candidates;
    }

    std::filesystem::path base;
#ifdef _WIN32
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local)
        base = std::filesystem::path(local) / "Narval";
#else
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg)
        base = std::filesystem::path(xdg) / "narval";
    else if (const char* home = std::getenv("HOME"); home && *home)
        base = std::filesystem::path(home) / ".cache" / "narval";
#endif
    if (base.empty()) {
        std::error_code ec;
        base = std::filesystem::temp_directory_path(ec) / "narval";
        if (ec) return candidates;
    }
    candidates.push_back(base / kVersion);
    return candidates;
}

}  // namespace

std::string narval_home_dir() {
    const auto candidates = home_candidates();
    return candidates.empty() ? std::string{} : candidates.front().string();
}

std::string ensure_embedded_assets() {
    const auto candidates = home_candidates();
    for (const auto& home : candidates) {
        std::error_code ec;
        std::filesystem::create_directories(home, ec);
        if (ec) continue;

        bool ok = write_if_stale(home / "runtime.o",
                                 narval_runtime_obj, narval_runtime_obj_len);
        // Empty where the platform has no no_std runtime (Windows): not an error.
        if (narval_runtime_nostd_obj_len)
            ok = write_if_stale(home / "runtime_nostd.o",
                                narval_runtime_nostd_obj, narval_runtime_nostd_obj_len) && ok;
        for (unsigned int i = 0; i < narval_stdlib_files_count; ++i) {
            const auto& file = narval_stdlib_files[i];
            ok = write_if_stale(home / "stdlib" / file.name, file.data, file.len) && ok;
        }
        if (!ok) continue;  // unwritable directory: try the next candidate

        export_env("NARVAL_HOME", home.string());
        // Only when the caller has not chosen a stdlib of their own.
        if (const char* chosen = std::getenv("NARVAL_STDLIB"); !chosen || !*chosen)
            export_env("NARVAL_STDLIB", (home / "stdlib").string());
        return home.string();
    }
    return {};
}

}  // namespace narval
