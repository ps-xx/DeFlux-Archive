#pragma once

// Task #9B: research launcher orchestration (preflight + explicit --inject).

#include "deflux/game/gta_sa_de/research_remote_loader.hpp"

#include <string>

namespace deflux::game::gta_sa_de {

struct ResearchLauncherResult {
    bool ok = false;
    bool inject_requested = false;
    ResearchLoadResult load_result = ResearchLoadResult::LoadNotRequested;
    std::string message;
    std::string executable_path;
    std::string sha256_hex;
    ResearchLoadDiagnostics load_diagnostics{};
};

ResearchLauncherResult run_research_launcher(int argc, char** argv) noexcept;

} // namespace deflux::game::gta_sa_de
