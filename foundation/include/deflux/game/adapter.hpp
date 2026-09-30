#pragma once

#include "deflux/version/version.hpp"

#include <cstdint>

namespace deflux::game {

struct AdapterIdentity {
    const char* id = "";
    const char* display_name = "";
};

enum class ProbeStatus : std::uint8_t {
    NotImplemented = 0,
    Unavailable,
    Unsupported,
    Supported,
    Failed,
};

enum class Readiness : std::uint8_t {
    NotImplemented = 0,
    Unavailable,
    NotReady,
    Ready,
};

struct ProbeResult {
    ProbeStatus status = ProbeStatus::NotImplemented;
    const char* message = "";
};

struct ReadinessResult {
    Readiness status = Readiness::NotImplemented;
    const char* message = "";
};

const char* to_string(ProbeStatus status) noexcept;
const char* to_string(Readiness status) noexcept;

inline bool succeeded(ProbeStatus status) noexcept {
    return status == ProbeStatus::Supported;
}

inline bool succeeded(Readiness status) noexcept {
    return status == Readiness::Ready;
}

inline bool succeeded(const ProbeResult& result) noexcept {
    return succeeded(result.status);
}

inline bool succeeded(const ReadinessResult& result) noexcept {
    return succeeded(result.status);
}

// Game-agnostic adapter contract. Implementations must not leak engine memory,
// offsets, UE4 types, or story-mode concepts through this interface.
class IGameAdapter {
public:
    virtual ~IGameAdapter() = default;

    virtual AdapterIdentity identity() const = 0;
    virtual version::SemanticVersion api_version() const = 0;

    // Is a supported game process/build available to this adapter?
    virtual ProbeResult probe_supported_game() const = 0;

    // Detected game build description when probing is implemented.
    // Must not reuse DeFlux application/protocol/API version numbers as a stand-in.
    virtual ProbeResult detected_game_build() const = 0;

    virtual ReadinessResult engine_ready() const = 0;
    virtual ReadinessResult world_ready() const = 0;
    virtual ReadinessResult local_player_ready() const = 0;
};

} // namespace deflux::game
