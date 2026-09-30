#include "deflux/game/gta_sa_de/research_launcher.hpp"

#include <cstdio>

int main(int argc, char** argv) {
    const auto result = deflux::game::gta_sa_de::run_research_launcher(argc, argv);
    std::printf("DeFlux Research Launcher (Task #9B)\n");
    std::printf("  preflight: %s\n", (result.ok || result.inject_requested) ? "PASS" : "FAIL");
    std::printf("  message: %s\n", result.message.c_str());
    if (!result.executable_path.empty()) {
        std::printf("  executable: %s\n", result.executable_path.c_str());
    }
    if (!result.sha256_hex.empty()) {
        std::printf("  sha256: %s\n", result.sha256_hex.c_str());
    }
    if (result.inject_requested) {
        std::printf("  inject: requested\n");
        std::printf("  load_result: %s\n",
                    deflux::game::gta_sa_de::to_string(result.load_result));
        if (result.load_diagnostics.win32_error != 0) {
            std::printf("  win32_error: %u\n", result.load_diagnostics.win32_error);
        }
        if (!result.load_diagnostics.runtime_dll_path.empty()) {
            std::printf("  runtime_dll: %s\n", result.load_diagnostics.runtime_dll_path.c_str());
        }
        std::printf("  cleanup_complete: %s\n",
                    result.load_diagnostics.cleanup_complete ? "yes" : "no");
        return result.load_result == deflux::game::gta_sa_de::ResearchLoadResult::LoadSucceeded
            ? 0
            : 2;
    }
    return result.ok ? 0 : 1;
}
