#pragma once

// Task #9B Gate 2B: narrowly scoped runtime research activation from validated intent.

namespace deflux::runtime {

void try_activate_research_from_intent() noexcept;

bool research_bootstrap_complete() noexcept;

} // namespace deflux::runtime
