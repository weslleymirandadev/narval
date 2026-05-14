#include "lsp/server.hpp"

int main() {
    try {
        narval::lsp::LSPServer server;
        server.run();
        return 0;
    } catch (...) {
        return 1;
    }
}
