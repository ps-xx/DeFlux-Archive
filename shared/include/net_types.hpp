#pragma once

#include "protocol.hpp"

#include <cstdint>
#include <cstring>

namespace deflux::net {

// ---------------------------------------------------------------------------
// Math / transform types aligned for GTA SA DE / UE4-style layouts
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vector3() = default;
    Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

struct Rotator {
    float pitch = 0.0f;
    float yaw   = 0.0f;
    float roll  = 0.0f;

    Rotator() = default;
    Rotator(float pitch_, float yaw_, float roll_)
        : pitch(pitch_), yaw(yaw_), roll(roll_) {}
};

// Compact player replication state matching client/server sync packets.
struct PlayerState {
    std::uint32_t slot       = 0;
    std::uint32_t net_id     = 0;
    Vector3       position{};
    Rotator       rotation{};
    Vector3       velocity{};
    float         health     = 100.0f;
    float         armour     = 0.0f;
    std::uint16_t weapon_id  = 0;
    std::uint8_t  flags      = 0; // bit0: on_foot, bit1: in_vehicle, bit2: aiming
    std::uint8_t  reserved   = 0;
};

struct PlayerSyncPayload {
    PlayerState state;
};
#pragma pack(pop)

static_assert(sizeof(Vector3) == 12, "Vector3 must be 12 bytes");
static_assert(sizeof(Rotator) == 12, "Rotator must be 12 bytes");
static_assert(sizeof(PlayerState) == 56, "PlayerState layout mismatch");
static_assert(sizeof(PlayerSyncPayload) == sizeof(PlayerState));

inline constexpr std::uint8_t kFlagOnFoot    = 1u << 0;
inline constexpr std::uint8_t kFlagInVehicle = 1u << 1;
inline constexpr std::uint8_t kFlagAiming    = 1u << 2;

inline void clear_player_state(PlayerState& state) {
    std::memset(&state, 0, sizeof(PlayerState));
    state.health = 100.0f;
}

} // namespace deflux::net
