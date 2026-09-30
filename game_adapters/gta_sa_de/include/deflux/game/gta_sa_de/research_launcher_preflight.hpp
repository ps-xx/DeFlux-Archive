#pragma once

// Task #9B: research launcher preflight (Gate 1 — no injection).

#include <string>

namespace deflux::game::gta_sa_de {

struct ResearchLauncherPreflightResult {
    bool ok = false;
    bool inject_requested = false;
    std::string message;
    std::string executable_path;
    std::string sha256_hex;
};

ResearchLauncherPreflightResult run_research_launcher_preflight(int argc, char** argv);

} // namespace deflux::game::gta_sa_de
