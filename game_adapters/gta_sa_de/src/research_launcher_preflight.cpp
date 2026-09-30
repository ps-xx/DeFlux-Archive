#include "deflux/game/gta_sa_de/research_launcher_preflight.hpp"

#include "deflux/game/gta_sa_de/research_launcher.hpp"

namespace deflux::game::gta_sa_de {

ResearchLauncherPreflightResult run_research_launcher_preflight(int argc, char** argv) {
    const ResearchLauncherResult launcher = run_research_launcher(argc, argv);
    ResearchLauncherPreflightResult result{};
    result.ok = launcher.ok && !launcher.inject_requested;
    result.inject_requested = launcher.inject_requested;
    result.message = launcher.message;
    result.executable_path = launcher.executable_path;
    result.sha256_hex = launcher.sha256_hex;
    if (launcher.inject_requested && launcher.ok) {
        result.ok = launcher.load_result == ResearchLoadResult::LoadSucceeded;
        result.message = launcher.message;
    }
    if (!launcher.inject_requested && launcher.ok) {
        result.message = "Research launcher preflight passed (no injection requested).";
    }
    return result;
}

} // namespace deflux::game::gta_sa_de
