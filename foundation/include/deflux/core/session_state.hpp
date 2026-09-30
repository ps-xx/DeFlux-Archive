#pragma once

#include <cstdint>

namespace deflux::core {

// Client session lifecycle. This is a model only: no transitions are performed.
enum class SessionState : std::uint8_t {
    Disconnected = 0,
    Connecting,
    Handshaking,
    Connected,
    WorldLoading,
    Ready,
    Spawning,
    Playing,
};

// Architecture names: DISCONNECTED, CONNECTING, HANDSHAKING, CONNECTED,
// WORLD_LOADING, READY, SPAWNING, PLAYING. Never returns nullptr.
const char* to_string(SessionState state) noexcept;

} // namespace deflux::core
