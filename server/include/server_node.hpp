#pragma once

#include "net_types.hpp"
#include "player_registry.hpp"
#include "protocol.hpp"

#include <atomic>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace deflux::server {

class ServerNode {
public:
    ServerNode();
    ~ServerNode();

    ServerNode(const ServerNode&) = delete;
    ServerNode& operator=(const ServerNode&) = delete;

    bool start(std::uint16_t port = protocol::kDefaultPort);
    void stop();

    // Single-threaded fixed-rate tick loop (60 FPS).
    void run();

    void request_shutdown();
    bool is_running() const noexcept { return m_running.load(std::memory_order_acquire); }

private:
    void tick(double dt_seconds);
    void drain_socket();
    void handle_datagram(const sockaddr_in& from, const std::uint8_t* data, int length);
    bool send_to(const sockaddr_in& to, protocol::PacketID id, const void* payload, std::uint16_t payload_size);

#if defined(_WIN32)
    SOCKET m_socket = INVALID_SOCKET;
#endif
    PlayerRegistry m_players;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_winsock_ready{false};
    std::uint32_t m_sequence = 0;
    std::uint16_t m_port = protocol::kDefaultPort;

    // Default freeroam spawn (Grove Street sidewalk). Override later via config.
    float m_spawn_x = 2495.0f;
    float m_spawn_y = -1680.0f;
    float m_spawn_z = 13.5f;
    float m_spawn_heading = 90.0f;
};

} // namespace deflux::server
