#pragma once

#include <string_view>

namespace deflux::game::gta_sa_de::detail {

bool fingerprint_is_registered(std::string_view sha256_hex) noexcept;

} // namespace deflux::game::gta_sa_de::detail
