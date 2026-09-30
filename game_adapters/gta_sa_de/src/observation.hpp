#pragma once

#include "deflux/core/process_lifecycle.hpp"

#include <string_view>

namespace deflux::game::gta_sa_de {

deflux::core::ProcessSample sample_live_process();

bool executable_paths_match(std::string_view utf8_left, std::string_view utf8_right);

} // namespace deflux::game::gta_sa_de
