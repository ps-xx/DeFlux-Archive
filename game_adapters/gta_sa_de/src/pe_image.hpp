#pragma once

#include "deflux/game/gta_sa_de/qualification.hpp"

namespace deflux::game::gta_sa_de::detail {

constexpr std::uint32_t kScnMemExecute = 0x20000000u;

bool read_u16(const std::uint8_t* data, std::size_t size, std::size_t offset, std::uint16_t& out) noexcept;
bool read_u32(const std::uint8_t* data, std::size_t size, std::size_t offset, std::uint32_t& out) noexcept;

} // namespace deflux::game::gta_sa_de::detail
