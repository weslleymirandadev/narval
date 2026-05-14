#include "lsp/path_utils.hpp"

#include <algorithm>
#include <cctype>

namespace narval::lsp {

std::string uri_to_path(std::string uri) {
    const std::string prefix = "file://";
    if (uri.rfind(prefix, 0) == 0) {
        uri = uri.substr(prefix.size());
    }

#ifdef _WIN32
    if (uri.size() >= 3 && uri[0] == '/' &&
        std::isalpha(static_cast<unsigned char>(uri[1])) && uri[2] == ':') {
        uri.erase(uri.begin());
    }
    std::replace(uri.begin(), uri.end(), '/', '\\');
#endif

    return uri;
}

std::string path_to_uri(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    if (!path.empty() && path[0] == '/') {
        return "file://" + path;
    }

    return "file:///" + path;
}

} // namespace narval::lsp
