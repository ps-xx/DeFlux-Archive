#include "deflux/core/process_lifecycle.hpp"
#include "deflux/core/session_state.hpp"
#include "deflux/game/adapter.hpp"
#include "deflux/game/gta_sa_de/adapter.hpp"
#include "deflux/game/gta_sa_de/qualification.hpp"
#include "deflux/version/version.hpp"

#if defined(_WINDOWS_) || defined(_WINDEF_) || defined(_WINNT_) || defined(WINAPI)
#error "Foundation/adapter headers must not pull in Windows headers."
#endif
#if defined(GWorld) || defined(UWorld) || defined(APawn) || defined(ENGINE_API)
#error "Foundation/adapter headers must not pull in UE4/GTA headers."
#endif

#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace deflux_tests {
int run_gta_sa_de_detection_tests(int& passed, int& failed);
int run_process_lifecycle_tests(int& passed, int& failed);
int run_qualification_tests(int& passed, int& failed);
int run_qualification_discovery_tests(int& passed, int& failed);
int run_qualification_ggame_state_observation_tests(int& passed, int& failed);
int run_qualification_legacy_chain_diagnostic_tests(int& passed, int& failed);
int run_qualification_local_player_semantic_research_tests(int& passed, int& failed);
int run_qualification_behavioral_transform_tests(int& passed, int& failed);
int run_qualification_grounded_transform_tests(int& passed, int& failed);
int run_qualification_in_process_research_tests(int& passed, int& failed);
int run_qualification_research_remote_loader_tests(int& passed, int& failed);
int run_qualification_research_activation_tests(int& passed, int& failed);
int run_qualification_live_observation_tests(int& passed, int& failed);
int run_qualification_structural_probe_tests(int& passed, int& failed);
int run_qualification_preflight_tests(int& passed, int& failed);
int run_m1_runtime_directory_tests(int& passed, int& failed);
}

namespace {

int g_failed = 0;
int g_passed = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    if (condition) {
        ++g_passed;
        return;
    }
    ++g_failed;
    std::printf("FAIL %s:%d: %s\n", file, line, expression);
}

#define CHECK(expr) check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

void test_session_state_names() {
    using deflux::core::SessionState;
    using deflux::core::to_string;

    CHECK(std::strcmp(to_string(SessionState::Disconnected), "DISCONNECTED") == 0);
    CHECK(std::strcmp(to_string(SessionState::Connecting), "CONNECTING") == 0);
    CHECK(std::strcmp(to_string(SessionState::Handshaking), "HANDSHAKING") == 0);
    CHECK(std::strcmp(to_string(SessionState::Connected), "CONNECTED") == 0);
    CHECK(std::strcmp(to_string(SessionState::WorldLoading), "WORLD_LOADING") == 0);
    CHECK(std::strcmp(to_string(SessionState::Ready), "READY") == 0);
    CHECK(std::strcmp(to_string(SessionState::Spawning), "SPAWNING") == 0);
    CHECK(std::strcmp(to_string(SessionState::Playing), "PLAYING") == 0);
    CHECK(std::strcmp(to_string(static_cast<SessionState>(255)), "UNKNOWN") == 0);
}

void test_version_consistency() {
    using deflux::version::current;
    using deflux::version::format;
    using deflux::version::kApplication;
    using deflux::version::kCurrent;
    using deflux::version::kGameAdapterApi;
    using deflux::version::kProtocol;
    using deflux::version::kRuntimeAbi;
    using deflux::version::VersionSet;

    CHECK(kApplication == kCurrent.application);
    CHECK(kProtocol == kCurrent.protocol);
    CHECK(kGameAdapterApi == kCurrent.game_adapter_api);
    CHECK(current() == kCurrent);
    CHECK(kRuntimeAbi == 1u);
    CHECK(kRuntimeAbi != kApplication.major);
    CHECK(kRuntimeAbi != kProtocol.major);
    CHECK(kRuntimeAbi != kGameAdapterApi.major);

    char application[32]{};
    char protocol[32]{};
    char adapter_api[32]{};
    CHECK(format(kApplication, application, sizeof(application)));
    CHECK(format(kProtocol, protocol, sizeof(protocol)));
    CHECK(format(kGameAdapterApi, adapter_api, sizeof(adapter_api)));
    CHECK(std::strcmp(application, "0.1.0") == 0);
    CHECK(std::strcmp(protocol, "0.1.0") == 0);
    CHECK(std::strcmp(adapter_api, "0.1.0") == 0);

    CHECK(sizeof(VersionSet) == 3 * sizeof(deflux::version::SemanticVersion));

    CHECK(!format(kApplication, nullptr, 16));
    char too_small[4]{};
    CHECK(!format(kApplication, too_small, sizeof(too_small)));
}

void test_gta_sa_de_adapter_identity_and_readiness() {
    const deflux::game::gta_sa_de::GtaSaDeAdapter adapter;
    const auto identity = adapter.identity();

    CHECK(identity.id != nullptr);
    CHECK(identity.display_name != nullptr);
    CHECK(std::strcmp(identity.id, "gta_sa_de") == 0);
    CHECK(std::strcmp(identity.display_name,
                      "Grand Theft Auto: San Andreas - The Definitive Edition") == 0);
    CHECK(adapter.api_version() == deflux::version::kGameAdapterApi);

    const auto probe = adapter.probe_supported_game();
    const auto build = adapter.detected_game_build();
    const auto engine = adapter.engine_ready();
    const auto world = adapter.world_ready();
    const auto local_player = adapter.local_player_ready();

    CHECK(probe.status != deflux::game::ProbeStatus::NotImplemented);
    CHECK(build.status != deflux::game::ProbeStatus::NotImplemented);
    CHECK(probe.status != deflux::game::ProbeStatus::Supported);
    CHECK(engine.status == deflux::game::Readiness::NotImplemented);
    CHECK(world.status == deflux::game::Readiness::NotImplemented);
    CHECK(local_player.status == deflux::game::Readiness::NotImplemented);

    CHECK(!deflux::game::succeeded(probe));
    CHECK(!deflux::game::succeeded(build));
    CHECK(!deflux::game::succeeded(engine));
    CHECK(!deflux::game::succeeded(world));
    CHECK(!deflux::game::succeeded(local_player));

    CHECK(std::strcmp(deflux::game::to_string(engine.status), "NOT_IMPLEMENTED") == 0);
    CHECK(probe.message != nullptr && probe.message[0] != '\0');
    CHECK(build.message != nullptr && build.message[0] != '\0');
}

} // namespace

int main(int argc, char** argv) {
    _putenv_s("DEFLUX_RESEARCH_ISOLATE_LIVE_TARGET", "1");
    const bool behavioral_only =
        argc > 1 && std::strcmp(argv[1], "--behavioral-only") == 0;
    const bool grounded_only =
        argc > 1 && std::strcmp(argv[1], "--grounded-only") == 0;
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    std::printf("DeFlux foundation tests\n");
    if (behavioral_only) {
        deflux_tests::run_qualification_behavioral_transform_tests(g_passed, g_failed);
        std::printf("%d passed, %d failed\n", g_passed, g_failed);
        return g_failed == 0 ? 0 : 1;
    }
    if (grounded_only) {
        deflux_tests::run_qualification_grounded_transform_tests(g_passed, g_failed);
        std::printf("%d passed, %d failed\n", g_passed, g_failed);
        return g_failed == 0 ? 0 : 1;
    }
    test_session_state_names();
    test_version_consistency();
    test_gta_sa_de_adapter_identity_and_readiness();
    deflux_tests::run_gta_sa_de_detection_tests(g_passed, g_failed);
    deflux_tests::run_process_lifecycle_tests(g_passed, g_failed);
    deflux_tests::run_qualification_tests(g_passed, g_failed);
    deflux_tests::run_qualification_discovery_tests(g_passed, g_failed);
    deflux_tests::run_qualification_ggame_state_observation_tests(g_passed, g_failed);
    deflux_tests::run_qualification_legacy_chain_diagnostic_tests(g_passed, g_failed);
    deflux_tests::run_qualification_local_player_semantic_research_tests(g_passed, g_failed);
    deflux_tests::run_qualification_behavioral_transform_tests(g_passed, g_failed);
    deflux_tests::run_qualification_grounded_transform_tests(g_passed, g_failed);
    deflux_tests::run_qualification_in_process_research_tests(g_passed, g_failed);
    deflux_tests::run_qualification_research_remote_loader_tests(g_passed, g_failed);
    deflux_tests::run_qualification_research_activation_tests(g_passed, g_failed);
    deflux_tests::run_qualification_live_observation_tests(g_passed, g_failed);
    deflux_tests::run_qualification_structural_probe_tests(g_passed, g_failed);
    deflux_tests::run_qualification_preflight_tests(g_passed, g_failed);
    deflux_tests::run_m1_runtime_directory_tests(g_passed, g_failed);
    std::printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
