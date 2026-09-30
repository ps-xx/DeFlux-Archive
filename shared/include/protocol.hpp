#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace deflux::protocol {

// ---------------------------------------------------------------------------
// Fixed network constants
// ---------------------------------------------------------------------------
inline constexpr std::uint16_t kDefaultPort      = 7777;
inline constexpr std::size_t   kMaxPacketSize    = 1200;   // Stay under typical MTU
inline constexpr std::size_t   kMaxPayloadSize   = kMaxPacketSize - 16;
inline constexpr std::uint32_t kProtocolMagic    = 0x44464C58; // 'DFLX'
inline constexpr std::uint16_t kProtocolVersion  = 1;
inline constexpr std::uint32_t kMaxPlayers       = 64;
inline constexpr std::uint32_t kTickRateHz       = 60;
inline constexpr std::uint32_t kClientSyncRateHz = 25; // PLAYER_SYNC send rate (20-30 Hz)

// ---------------------------------------------------------------------------
// Packet identifiers
// ---------------------------------------------------------------------------
enum class PacketID : std::uint16_t {
    CONNECT         = 0x0001,
    DISCONNECT      = 0x0002,
    PLAYER_SYNC     = 0x0003,
    PING            = 0x0004,
    SPAWN_REQUEST   = 0x0005,
    SPAWN_RESPONSE  = 0x0006,
};

// SPAWN_RESPONSE.flags
inline constexpr std::uint32_t kSpawnFlagFreeroam   = 1u << 0;
inline constexpr std::uint32_t kSpawnFlagStripHud   = 1u << 1;
inline constexpr std::uint32_t kSpawnFlagNoMissions = 1u << 2;

// ---------------------------------------------------------------------------
// Wire header (packed, little-endian on the wire)
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct PacketHeader {
    std::uint32_t magic;     // kProtocolMagic
    std::uint16_t version;   // kProtocolVersion
    PacketID      id;
    std::uint16_t length;    // Payload bytes following this header
    std::uint32_t sequence;
};

struct ConnectPayload {
    char          name[32];
    std::uint32_t client_build;
};

struct DisconnectPayload {
    std::uint16_t reason_code;
    char          reason[64];
};

struct PingPayload {
    std::uint64_t client_timestamp_ms;
    std::uint64_t server_timestamp_ms;
};

// Client asks the server for an authoritative freeroam spawn (MTA-style join).
struct SpawnRequestPayload {
    std::uint32_t client_build;
    std::uint32_t flags; // reserved
};

// Server reply: slot/net_id + world coordinates (SA classic XYZ).
struct SpawnResponsePayload {
    std::uint32_t slot;
    std::uint32_t net_id;
    float         x;
    float         y;
    float         z;
    float         heading;
    std::uint32_t flags; // kSpawnFlag*
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 14, "PacketHeader size mismatch");
static_assert(std::is_trivially_copyable_v<PacketHeader>);
static_assert(std::is_trivially_copyable_v<ConnectPayload>);
static_assert(std::is_trivially_copyable_v<DisconnectPayload>);
static_assert(std::is_trivially_copyable_v<PingPayload>);
static_assert(sizeof(SpawnRequestPayload) == 8, "SpawnRequestPayload size mismatch");
static_assert(sizeof(SpawnResponsePayload) == 28, "SpawnResponsePayload size mismatch");
static_assert(std::is_trivially_copyable_v<SpawnRequestPayload>);
static_assert(std::is_trivially_copyable_v<SpawnResponsePayload>);

// ---------------------------------------------------------------------------
// Byte serialization helpers (POD, zero-copy friendly)
// ---------------------------------------------------------------------------
template <typename T>
inline bool write_bytes(std::uint8_t* dst, std::size_t dst_size, std::size_t& offset, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    if (offset + sizeof(T) > dst_size) {
        return false;
    }
    std::memcpy(dst + offset, &value, sizeof(T));
    offset += sizeof(T);
    return true;
}

template <typename T>
inline bool read_bytes(const std::uint8_t* src, std::size_t src_size, std::size_t& offset, T& value) {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    if (offset + sizeof(T) > src_size) {
        return false;
    }
    std::memcpy(&value, src + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

inline bool make_header(PacketHeader& out,
                        PacketID id,
                        std::uint16_t payload_length,
                        std::uint32_t sequence) {
    out.magic    = kProtocolMagic;
    out.version  = kProtocolVersion;
    out.id       = id;
    out.length   = payload_length;
    out.sequence = sequence;
    return true;
}

inline bool validate_header(const PacketHeader& header) {
    if (header.magic != kProtocolMagic) {
        return false;
    }
    if (header.version != kProtocolVersion) {
        return false;
    }
    if (header.length > kMaxPayloadSize) {
        return false;
    }
    return true;
}

} // namespace deflux::protocol
