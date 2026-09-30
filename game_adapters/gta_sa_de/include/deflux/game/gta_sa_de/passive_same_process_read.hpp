#pragma once

// Task #9B: bounded same-process PASSIVE_READ helper (research only).

#include <cstddef>
#include <cstdint>
#include <string>

namespace deflux::game::gta_sa_de {

inline constexpr std::size_t kPassiveReadMaxBytesPerSample = 1;
inline constexpr std::size_t kPassiveReadMaxPointerDepth = 0;

struct PassiveReadResult {
    bool ok = false;
    std::uint8_t byte = 0;
    std::string failure_reason;
};

PassiveReadResult passive_read_byte(const void* address) noexcept;

} // namespace deflux::game::gta_sa_de
