#include "player_registry.hpp"

#include <cstring>

namespace deflux::server {

PlayerRegistry::PlayerRegistry() {
    for (std::uint32_t i = 0; i < m_sessions.size(); ++i) {
        m_sessions[i].slot = i;
        net::clear_player_state(m_sessions[i].state);
    }
}

bool PlayerRegistry::endpoints_equal(const sockaddr_in& a, const sockaddr_in& b) noexcept {
    return a.sin_family == b.sin_family
        && a.sin_port == b.sin_port
        && a.sin_addr.s_addr == b.sin_addr.s_addr;
}

std::string PlayerRegistry::endpoint_key(const sockaddr_in& endpoint) {
    char ip[INET_ADDRSTRLEN]{};
    InetNtopA(AF_INET, &endpoint.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(endpoint.sin_port));
}

std::optional<std::uint32_t> PlayerRegistry::allocate(const sockaddr_in& endpoint, const char* name) {
    if (auto* existing = find_by_endpoint(endpoint)) {
        if (name != nullptr) {
            strncpy_s(existing->name, sizeof(existing->name), name, _TRUNCATE);
        }
        return existing->slot;
    }

    if (m_active_count >= protocol::kMaxPlayers) {
        return std::nullopt;
    }

    for (auto& session : m_sessions) {
        if (session.active) {
            continue;
        }

        session.active = true;
        session.endpoint = endpoint;
        session.net_id = m_next_net_id++;
        net::clear_player_state(session.state);
        session.state.slot = session.slot;
        session.state.net_id = session.net_id;

        std::memset(session.name, 0, sizeof(session.name));
        if (name != nullptr) {
            strncpy_s(session.name, sizeof(session.name), name, _TRUNCATE);
        } else {
            strncpy_s(session.name, sizeof(session.name), "Player", _TRUNCATE);
        }

        ++m_active_count;
        (void)endpoint_key(endpoint); // Reserved for future hashmap indexing.
        return session.slot;
    }

    return std::nullopt;
}

void PlayerRegistry::release_by_endpoint(const sockaddr_in& endpoint) {
    if (auto* session = find_by_endpoint(endpoint)) {
        release_slot(session->slot);
    }
}

void PlayerRegistry::release_slot(std::uint32_t slot) {
    if (slot >= m_sessions.size()) {
        return;
    }

    auto& session = m_sessions[slot];
    if (!session.active) {
        return;
    }

    session.active = false;
    session.net_id = 0;
    session.last_pos_log_ms = 0;
    std::memset(&session.endpoint, 0, sizeof(session.endpoint));
    std::memset(session.name, 0, sizeof(session.name));
    net::clear_player_state(session.state);
    session.state.slot = slot;

    if (m_active_count > 0) {
        --m_active_count;
    }
}

PlayerSession* PlayerRegistry::find_by_endpoint(const sockaddr_in& endpoint) {
    for (auto& session : m_sessions) {
        if (session.active && endpoints_equal(session.endpoint, endpoint)) {
            return &session;
        }
    }
    return nullptr;
}

PlayerSession* PlayerRegistry::find_by_slot(std::uint32_t slot) {
    if (slot >= m_sessions.size()) {
        return nullptr;
    }
    auto& session = m_sessions[slot];
    return session.active ? &session : nullptr;
}

PlayerSession* PlayerRegistry::apply_player_sync(const sockaddr_in& endpoint,
                                                const net::PlayerState& state) {
    PlayerSession* session = find_by_endpoint(endpoint);
    if (session == nullptr) {
        // Late-join path: accept sync from an endpoint that skipped/missed CONNECT.
        const auto slot = allocate(endpoint, "DeFluxPlayer");
        if (!slot.has_value()) {
            return nullptr;
        }
        session = find_by_slot(*slot);
        if (session == nullptr) {
            return nullptr;
        }
    }

    session->state.position = state.position;
    session->state.rotation = state.rotation;
    session->state.velocity = state.velocity;
    session->state.health = state.health;
    session->state.armour = state.armour;
    session->state.weapon_id = state.weapon_id;
    session->state.flags = state.flags;
    session->state.slot = session->slot;
    session->state.net_id = session->net_id;
    return session;
}

} // namespace deflux::server
