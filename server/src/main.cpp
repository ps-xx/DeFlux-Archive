#include "server_node.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>

namespace {
std::atomic<deflux::server::ServerNode*> g_server{nullptr};

void on_signal(int) {
    if (auto* server = g_server.load(std::memory_order_acquire)) {
        server->request_shutdown();
    }
}
} // namespace

int main(int argc, char** argv) {
    std::uint16_t port = deflux::protocol::kDefaultPort;
    if (argc >= 2) {
        const int parsed = std::atoi(argv[1]);
        if (parsed > 0 && parsed <= 65535) {
            port = static_cast<std::uint16_t>(parsed);
        }
    }

    deflux::server::ServerNode server;
    g_server.store(&server, std::memory_order_release);

    std::signal(SIGINT, on_signal);
#if defined(SIGTERM)
    std::signal(SIGTERM, on_signal);
#endif

    std::printf("[DeFlux_Server] Starting dedicated server on UDP %u\n",
                static_cast<unsigned>(port));

    if (!server.start(port)) {
        std::fprintf(stderr, "[DeFlux_Server] Failed to bind UDP socket.\n");
        g_server.store(nullptr, std::memory_order_release);
        return 1;
    }

    server.run();

    server.stop();
    g_server.store(nullptr, std::memory_order_release);
    std::printf("[DeFlux_Server] Shutdown complete.\n");
    return 0;
}
