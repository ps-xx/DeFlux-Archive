#pragma once

// Test/harness-only seam for InProcessResearchController. Not part of live research config.

#include "deflux/game/gta_sa_de/in_process_research.hpp"

#include <cstdint>
#include <string>

namespace deflux::game::gta_sa_de::detail {

bool in_process_research_test_sample_once(InProcessResearchController& controller,
                                          std::string& message_out);

bool in_process_research_test_force_derived_address(InProcessResearchController& controller,
                                                    std::uintptr_t address);

bool in_process_research_test_wait_until_observing(InProcessResearchController& controller,
                                                   std::uint32_t timeout_ms);

bool in_process_research_test_wait_until_substate(InProcessResearchController& controller,
                                                  ResearchSubstate expected,
                                                  std::uint32_t timeout_ms);

} // namespace deflux::game::gta_sa_de::detail
