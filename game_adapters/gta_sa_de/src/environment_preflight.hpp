#pragma once

#include "deflux/game/gta_sa_de/qualification.hpp"

#include <string_view>

namespace deflux::game::gta_sa_de::detail {

EnvironmentPreflightResult scan_game_executable_directory(std::string_view executable_utf8_path);

} // namespace deflux::game::gta_sa_de::detail
