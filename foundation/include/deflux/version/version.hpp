#pragma once

#include <cstddef>
#include <cstdint>

namespace deflux::version {

// Semantic version for DeFlux platform identities.
// The GTA game build is never stored in these fields.
struct SemanticVersion {
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint16_t patch = 0;

    constexpr bool operator==(const SemanticVersion&) const noexcept = default;
    constexpr bool operator!=(const SemanticVersion&) const noexcept = default;
};

// DeFlux application version (this product). Independent of any GTA build id.
inline constexpr SemanticVersion kApplication{0, 1, 0};

// M1 protocol identity. Not used on the wire yet. Independent of
// shared/include/protocol.hpp (legacy prototype packet version).
inline constexpr SemanticVersion kProtocol{0, 1, 0};

// Game Adapter interface contract version. Independent of GTA build and of
// the DeFlux application version (they may share numbers by coincidence).
inline constexpr SemanticVersion kGameAdapterApi{0, 1, 0};

// Bootstrap <-> in-process runtime ABI. Integer identity, not a SemVer and not
// a GTA fingerprint. Bump this when the C ABI in runtime/include is incompatible.
inline constexpr std::uint32_t kRuntimeAbi = 1;

struct VersionSet {
    SemanticVersion application{};
    SemanticVersion protocol{};
    SemanticVersion game_adapter_api{};

    constexpr bool operator==(const VersionSet&) const noexcept = default;
    constexpr bool operator!=(const VersionSet&) const noexcept = default;
};

inline constexpr VersionSet kCurrent{
    kApplication,
    kProtocol,
    kGameAdapterApi,
};

inline constexpr VersionSet current() noexcept {
    return kCurrent;
}

// Writes "major.minor.patch" (NUL-terminated) into buffer.
// Returns false if buffer is null or too small (need at least 6 bytes for 0.0.0).
bool format(const SemanticVersion& version, char* buffer, std::size_t buffer_size) noexcept;

} // namespace deflux::version
