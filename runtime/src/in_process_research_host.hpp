#pragma once

// M1 research composition: authoritative in-process research controller owned by
// DeFlux_Runtime.dll. Not part of the generic C runtime ABI.

#include "deflux/game/gta_sa_de/in_process_research.hpp"

namespace deflux::runtime {

deflux::game::gta_sa_de::InProcessResearchController& authoritative_research_controller()
    noexcept;

void shutdown_research_from_host() noexcept;

std::uintptr_t authoritative_research_controller_identity() noexcept;

deflux::game::gta_sa_de::ResearchSubstate authoritative_research_substate() noexcept;

} // namespace deflux::runtime
