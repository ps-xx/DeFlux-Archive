#pragma once

#include "net_types.hpp"
#include "protocol.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace deflux::server {

struct PlayerSession {
    bool          active = false;
    std::uint32_t slot = 0;
    std::uint32_t net_id = 0;
    sockaddr_in   endpoint{};
    char          name[32]{};
    net::PlayerState state{};
    std::uint64_t last_pos_log_ms = 0;
};

// Session management mapping active connections to (IP:Port) NAT pairs.
class PlayerRegistry {
public:
    PlayerRegistry();

    std::optional<std::uint32_t> allocate(const sockaddr_in& endpoint, const char* name);
    void release_by_endpoint(const sockaddr_in& endpoint);
    void release_slot(std::uint32_t slot);

    PlayerSession* find_by_endpoint(const sockaddr_in& endpoint);
    PlayerSession* find_by_slot(std::uint32_t slot);

    // Applies PLAYER_SYNC transform data into the session for this NAT endpoint.
    PlayerSession* apply_player_sync(const sockaddr_in& endpoint, const net::PlayerState& state);

    std::uint32_t active_count() const noexcept { return m_active_count; }
    static constexpr std::uint32_t max_players() noexcept { return protocol::kMaxPlayers; }

    template <typename Fn>
    void for_each_active(Fn&& fn) {
        for (auto& session : m_sessions) {
            if (session.active) {
                fn(session);
            }
        }
    }

private:
    static bool endpoints_equal(const sockaddr_in& a, const sockaddr_in& b) noexcept;
    static std::string endpoint_key(const sockaddr_in& endpoint);

    std::array<PlayerSession, protocol::kMaxPlayers> m_sessions{};
    std::uint32_t m_active_count = 0;
    std::uint32_t m_next_net_id = 1;
};

} // namespace deflux::server
