#include "lsp/transport.hpp"

#include <iostream>
#include <string>

namespace narval::lsp {

std::optional<json> LSPTransport::read_message() {
    std::string line;
    size_t content_length = 0;

    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }

        const std::string header = "Content-Length:";
        if (line.rfind(header, 0) == 0) {
            content_length = static_cast<size_t>(std::stoul(line.substr(header.size())));
        }
    }

    if (!std::cin || content_length == 0) {
        return std::nullopt;
    }

    std::string body(content_length, '\0');
    std::cin.read(body.data(), static_cast<std::streamsize>(content_length));
    if (!std::cin) {
        return std::nullopt;
    }

    return json::parse(body);
}

void LSPTransport::send_message(const json& message) {
    const std::string body = message.dump();
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    std::cout.flush();
}

} // namespace narval::lsp
