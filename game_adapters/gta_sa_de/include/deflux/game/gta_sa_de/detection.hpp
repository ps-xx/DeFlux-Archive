#pragma once

#include "deflux/game/adapter.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace deflux::game::gta_sa_de {

enum class InstallationStatus : std::uint8_t {
    Found = 0,
    NotFound,
    Error,
};

enum class Compatibility : std::uint8_t {
    Supported = 0,
    DetectedButUnsupported,
    NotFound,
    Error,
};

// Passive GTA SA DE install/process snapshot. No Windows types, handles, or
// Steam/registry details. Strings are UTF-8. Empty fields mean "not observed".
struct DetectionSnapshot {
    InstallationStatus installation = InstallationStatus::NotFound;
    Compatibility compatibility = Compatibility::NotFound;
    std::string executable_path;
    bool running = false;
    std::uint32_t process_id = 0;
    std::string sha256_hex;
    std::uint64_t file_size_bytes = 0;
    std::string file_version;
    std::string product_version;
    std::string message;
};

const char* to_string(InstallationStatus status) noexcept;
const char* to_string(Compatibility status) noexcept;

bool is_supported_fingerprint(std::string_view sha256_hex) noexcept;

// Pure policy. Never returns Supported unless fingerprint_ok && registered.
Compatibility compatibility_of(InstallationStatus installation,
                               bool fingerprint_ok,
                               bool fingerprint_registered) noexcept;

ProbeStatus to_probe_status(Compatibility compatibility) noexcept;

// UTF-8 paths. Slash unification, GetFullPathName, GetLongPathName, then
// case-insensitive compare. Returns false if either path is empty or not UTF-8.
bool executable_paths_match(std::string_view utf8_left, std::string_view utf8_right);

} // namespace deflux::game::gta_sa_de
