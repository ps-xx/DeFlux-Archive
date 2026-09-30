#pragma once

#include "deflux/game/gta_sa_de/detection.hpp"

#include <string_view>

namespace deflux::game::gta_sa_de {

DetectionSnapshot detect_installation();
DetectionSnapshot detect_explicit_executable(std::string_view utf8_path);

} // namespace deflux::game::gta_sa_de
