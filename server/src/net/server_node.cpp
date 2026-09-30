#include "server_node.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#pragma comment(lib, "Ws2_32.lib")

namespace deflux::server {

ServerNode::ServerNode() = default;

ServerNode::~ServerNode() {
    stop();
}

bool ServerNode::start(std::uint16_t port) {
    if (m_running.load(std::memory_order_acquire)) {
        return true;
    }

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return false;
    }
    m_winsock_ready.store(true, std::memory_order_release);

    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET) {
        WSACleanup();
        m_winsock_ready.store(false, std::memory_order_release);
        return false;
    }

    u_long non_blocking = 1;
    if (ioctlsocket(m_socket, FIONBIO, &non_blocking) != 0) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        WSACleanup();
        m_winsock_ready.store(false, std::memory_order_release);
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        WSACleanup();
        m_winsock_ready.store(false, std::memory_order_release);
        return false;
    }

    m_port = port;
    m_running.store(true, std::memory_order_release);
    std::printf("[DeFlux_Server] Listening on 0.0.0.0:%u (tick=%u Hz, max_players=%u)\n",
                static_cast<unsigned>(m_port),
                protocol::kTickRateHz,
                protocol::kMaxPlayers);
    std::printf("[DeFlux_Server] Freeroam spawn (%.1f, %.1f, %.1f) heading=%.1f\n",
                m_spawn_x,
                m_spawn_y,
                m_spawn_z,
                m_spawn_heading);
    return true;
}

void ServerNode::stop() {
    m_running.store(false, std::memory_order_release);

    if (m_socket != INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }

    if (m_winsock_ready.exchange(false, std::memory_order_acq_rel)) {
        WSACleanup();
    }
}

void ServerNode::request_shutdown() {
    m_running.store(false, std::memory_order_release);
}

void ServerNode::run() {
    using clock = std::chrono::steady_clock;
    constexpr auto tick_period = std::chrono::duration<double>(1.0 / protocol::kTickRateHz);

    auto previous = clock::now();

    while (m_running.load(std::memory_order_acquire)) {
        const auto frame_start = clock::now();
        const double dt = std::chrono::duration<double>(frame_start - previous).count();
        previous = frame_start;

        drain_socket();
        tick(dt);

        const auto elapsed = clock::now() - frame_start;
        if (elapsed < tick_period) {
            std::this_thread::sleep_for(
                std::chrono::duration_cast<std::chrono::milliseconds>(tick_period - elapsed));
        }
    }
}

void ServerNode::tick(double /*dt_seconds*/) {
    // Fixed-rate authoritative tick. Replication / interest management hooks go here.
}

void ServerNode::drain_socket() {
    if (m_socket == INVALID_SOCKET) {
        return;
    }

    std::uint8_t buffer[protocol::kMaxPacketSize]{};
    sockaddr_in from{};
    int from_len = sizeof(from);

    // Drain without lock contention (single-threaded owner).
    for (;;) {
        const int received = recvfrom(
            m_socket,
            reinterpret_cast<char*>(buffer),
            static_cast<int>(sizeof(buffer)),
            0,
            reinterpret_cast<sockaddr*>(&from),
            &from_len);

        if (received == SOCKET_ERROR) {
            const int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                break;
            }
            break;
        }

        if (received > 0) {
            handle_datagram(from, buffer, received);
        }
    }
}

void ServerNode::handle_datagram(const sockaddr_in& from, const std::uint8_t* data, int length) {
    if (data == nullptr || length < static_cast<int>(sizeof(protocol::PacketHeader))) {
        return;
    }

    protocol::PacketHeader header{};
    std::size_t offset = 0;
    if (!protocol::read_bytes(data, static_cast<std::size_t>(length), offset, header)) {
        return;
    }
    if (!protocol::validate_header(header)) {
        return;
    }

    switch (header.id) {
    case protocol::PacketID::CONNECT: {
        protocol::ConnectPayload payload{};
        if (!protocol::read_bytes(data, static_cast<std::size_t>(length), offset, payload)) {
            return;
        }
        const auto slot = m_players.allocate(from, payload.name);
        if (!slot.has_value()) {
            protocol::DisconnectPayload deny{};
            std::memset(&deny, 0, sizeof(deny));
            deny.reason_code = 1;
            strncpy_s(deny.reason, sizeof(deny.reason), "server_full", _TRUNCATE);
            (void)send_to(from, protocol::PacketID::DISCONNECT, &deny, static_cast<std::uint16_t>(sizeof(deny)));
            return;
        }
        std::printf("[DeFlux_Server] Player connected slot=%u name=%s active=%u\n",
                    *slot,
                    payload.name,
                    m_players.active_count());
        break;
    }
    case protocol::PacketID::DISCONNECT: {
        m_players.release_by_endpoint(from);
        std::printf("[DeFlux_Server] Player disconnected. active=%u\n", m_players.active_count());
        break;
    }
    case protocol::PacketID::PLAYER_SYNC: {
        net::PlayerSyncPayload payload{};
        if (!protocol::read_bytes(data, static_cast<std::size_t>(length), offset, payload)) {
            return;
        }

        PlayerSession* session = m_players.apply_player_sync(from, payload.state);
        if (session == nullptr) {
            return;
        }

        const auto now_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());

        // Throttle console spam: sample ~2 Hz per player.
        constexpr std::uint64_t kLogIntervalMs = 500;
        if (session->last_pos_log_ms == 0 || now_ms - session->last_pos_log_ms >= kLogIntervalMs) {
            session->last_pos_log_ms = now_ms;
            std::printf("[DeFlux_Server] Player [ID: %u] Pos: (X: %.2f, Y: %.2f, Z: %.2f)\n",
                        session->net_id,
                        session->state.position.x,
                        session->state.position.y,
                        session->state.position.z);
        }
        break;
    }
    case protocol::PacketID::PING: {
        protocol::PingPayload payload{};
        if (!protocol::read_bytes(data, static_cast<std::size_t>(length), offset, payload)) {
            return;
        }
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
        payload.server_timestamp_ms = static_cast<std::uint64_t>(now.count());
        (void)send_to(from, protocol::PacketID::PING, &payload, static_cast<std::uint16_t>(sizeof(payload)));
        break;
    }
    case protocol::PacketID::SPAWN_REQUEST: {
        protocol::SpawnRequestPayload req{};
        if (!protocol::read_bytes(data, static_cast<std::size_t>(length), offset, req)) {
            return;
        }

        PlayerSession* session = m_players.find_by_endpoint(from);
        if (session == nullptr) {
            // Late join without CONNECT race: allocate anonymous slot.
            const auto slot = m_players.allocate(from, "DeFluxPlayer");
            if (!slot.has_value()) {
                return;
            }
            session = m_players.find_by_slot(*slot);
        }
        if (session == nullptr) {
            return;
        }

        protocol::SpawnResponsePayload resp{};
        std::memset(&resp, 0, sizeof(resp));
        resp.slot = session->slot;
        resp.net_id = session->net_id;
        resp.x = m_spawn_x;
        resp.y = m_spawn_y;
        resp.z = m_spawn_z;
        resp.heading = m_spawn_heading;
        resp.flags = protocol::kSpawnFlagFreeroam | protocol::kSpawnFlagStripHud
            | protocol::kSpawnFlagNoMissions;
        if ((req.flags & protocol::kSpawnFlagStripHud) == 0) {
            // Still freeroam by default for MTA-style join.
            resp.flags |= protocol::kSpawnFlagFreeroam;
        }

        session->state.position = net::Vector3{resp.x, resp.y, resp.z};
        session->state.rotation.yaw = resp.heading;
        session->state.slot = resp.slot;
        session->state.net_id = resp.net_id;
        session->state.flags = net::kFlagOnFoot;

        (void)send_to(from,
                      protocol::PacketID::SPAWN_RESPONSE,
                      &resp,
                      static_cast<std::uint16_t>(sizeof(resp)));
        std::printf("[DeFlux_Server] SPAWN_RESPONSE slot=%u net_id=%u -> (%.1f, %.1f, %.1f)\n",
                    resp.slot,
                    resp.net_id,
                    resp.x,
                    resp.y,
                    resp.z);
        break;
    }
    default:
        break;
    }
}

bool ServerNode::send_to(const sockaddr_in& to,
                         protocol::PacketID id,
                         const void* payload,
                         std::uint16_t payload_size) {
    if (m_socket == INVALID_SOCKET || payload_size > protocol::kMaxPayloadSize) {
        return false;
    }

    std::uint8_t buffer[protocol::kMaxPacketSize]{};
    std::size_t offset = 0;

    protocol::PacketHeader header{};
    protocol::make_header(header, id, payload_size, m_sequence++);
    if (!protocol::write_bytes(buffer, sizeof(buffer), offset, header)) {
        return false;
    }

    if (payload != nullptr && payload_size > 0) {
        std::memcpy(buffer + offset, payload, payload_size);
        offset += payload_size;
    }

    const int sent = sendto(
        m_socket,
        reinterpret_cast<const char*>(buffer),
        static_cast<int>(offset),
        0,
        reinterpret_cast<const sockaddr*>(&to),
        sizeof(to));

    return sent == static_cast<int>(offset);
}

} // namespace deflux::server
