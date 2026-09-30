#include "deflux/core/process_lifecycle.hpp"
#include "deflux/game/gta_sa_de/adapter.hpp"
#include "deflux/game/gta_sa_de/qualification.hpp"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

namespace deflux_tests {
namespace {

namespace fs = std::filesystem;

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

#define CHECK_PF(expr) check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

class TempSandbox {
public:
    TempSandbox() {
        root_ = fs::temp_directory_path()
            / ("deflux_preflight_" + std::to_string(static_cast<unsigned long long>(std::time(nullptr)))
               + "_" + std::to_string(counter_++));
        fs::create_directories(root_);
        exe_path_ = (root_ / L"SanAndreas.exe").string();
        std::ofstream exe(exe_path_, std::ios::binary);
        exe.put('M');
        exe.put('Z');
    }

    ~TempSandbox() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    fs::path dir() const { return root_; }

    std::string exe() const { return exe_path_; }

    void touch(const wchar_t* name) {
        std::ofstream out(root_ / name, std::ios::binary);
        out.put('x');
    }

private:
    static inline int counter_ = 0;
    fs::path root_;
    std::string exe_path_;
};

void test_clean_directory_accepted() {
    TempSandbox sandbox;
    const auto preflight = deflux::game::gta_sa_de::scan_game_executable_directory(sandbox.exe());
    CHECK_PF(preflight.clean);
    CHECK_PF(preflight.contaminants.empty());
}

void test_version_dll_rejected() {
    TempSandbox sandbox;
    sandbox.touch(L"version.dll");
    const auto preflight = deflux::game::gta_sa_de::scan_game_executable_directory(sandbox.exe());
    CHECK_PF(!preflight.clean);
    CHECK_PF(preflight.contaminants.size() == 1);
    CHECK_PF(preflight.contaminants[0].filename == "version.dll");
}

void test_defluxcore_asi_rejected() {
    TempSandbox sandbox;
    sandbox.touch(L"DeFluxCore.asi");
    const auto preflight = deflux::game::gta_sa_de::scan_game_executable_directory(sandbox.exe());
    CHECK_PF(!preflight.clean);
    CHECK_PF(preflight.contaminants.size() == 1);
    CHECK_PF(preflight.contaminants[0].filename == "DeFluxCore.asi");
}

void test_defluxcore_dll_rejected() {
    TempSandbox sandbox;
    sandbox.touch(L"DeFluxCore.dll");
    const auto preflight = deflux::game::gta_sa_de::scan_game_executable_directory(sandbox.exe());
    CHECK_PF(!preflight.clean);
    CHECK_PF(preflight.contaminants.size() == 1);
    CHECK_PF(preflight.contaminants[0].filename == "DeFluxCore.dll");
}

void test_entry_mode_marker_rejected() {
    TempSandbox sandbox;
    sandbox.touch(L"deflux_entry_mode.txt");
    const auto preflight = deflux::game::gta_sa_de::scan_game_executable_directory(sandbox.exe());
    CHECK_PF(!preflight.clean);
    CHECK_PF(preflight.contaminants.size() == 1);
    CHECK_PF(preflight.contaminants[0].filename == "deflux_entry_mode.txt");
}

void test_multiple_contaminants_reported_deterministically() {
    TempSandbox sandbox;
    sandbox.touch(L"DeFluxCore.dll");
    sandbox.touch(L"version.dll");
    sandbox.touch(L"deflux_entry_mode.txt");
    const auto preflight = deflux::game::gta_sa_de::scan_game_executable_directory(sandbox.exe());
    CHECK_PF(!preflight.clean);
    CHECK_PF(preflight.contaminants.size() == 3);
    CHECK_PF(preflight.contaminants[0].filename == "DeFluxCore.dll");
    CHECK_PF(preflight.contaminants[1].filename == "deflux_entry_mode.txt");
    CHECK_PF(preflight.contaminants[2].filename == "version.dll");
}

void test_contamination_result_status() {
    TempSandbox sandbox;
    sandbox.touch(L"version.dll");
    const auto preflight = deflux::game::gta_sa_de::scan_game_executable_directory(sandbox.exe());
    const auto result = deflux::game::gta_sa_de::result_from_environment_contamination(preflight);
    CHECK_PF(result.status == deflux::game::gta_sa_de::QualificationStatus::EnvironmentContaminated);
    CHECK_PF(std::strcmp(deflux::game::gta_sa_de::to_string(result.status),
                         "ENVIRONMENT_CONTAMINATED") == 0);
    CHECK_PF(!result.message.empty());
}

void test_supported_build_registry_unchanged() {
    const deflux::game::gta_sa_de::GtaSaDeAdapter adapter;
    const auto probe = adapter.probe_supported_game();
    CHECK_PF(probe.status != deflux::game::ProbeStatus::Supported);
    CHECK_PF(!deflux::game::succeeded(probe));
}

void test_readiness_remains_not_implemented() {
    const deflux::game::gta_sa_de::GtaSaDeAdapter adapter;
    CHECK_PF(adapter.engine_ready().status == deflux::game::Readiness::NotImplemented);
    CHECK_PF(adapter.world_ready().status == deflux::game::Readiness::NotImplemented);
    CHECK_PF(adapter.local_player_ready().status == deflux::game::Readiness::NotImplemented);
}

void test_qualify_observation_stops_before_open_process() {
    TempSandbox sandbox;
    sandbox.touch(L"version.dll");

    deflux::core::ProcessObservation observation{};
    observation.state = deflux::core::ProcessLifecycle::ProcessDetected;
    observation.instance.process_id = 12345;
    observation.instance.creation_identity = 67890;
    observation.instance.executable_path = sandbox.exe();

    const auto result = deflux::game::gta_sa_de::qualify_observation(observation);
    CHECK_PF(result.status
             == deflux::game::gta_sa_de::QualificationStatus::EnvironmentContaminated);
    CHECK_PF(!result.message.empty());
}

void test_legacy_anchor_trust_label() {
    const auto* profile = deflux::game::gta_sa_de::find_qualification_profile(
        "63e633bb51c35fe35ca98f503b62e7ddb8403abe4c9d501cfa9162892453846f");
    CHECK_PF(profile != nullptr);
    CHECK_PF(profile->gworld_slot_rva == 0x05391B70u);
    CHECK_PF(profile->anchor_trust
             == deflux::game::gta_sa_de::GWorldAnchorTrust::LegacyCandidateUnverified);
    CHECK_PF(std::strcmp(deflux::game::gta_sa_de::to_string(profile->anchor_trust),
                         "LEGACY_CANDIDATE_UNVERIFIED") == 0);
}

} // namespace

int run_qualification_preflight_tests(int& passed, int& failed) {
    g_failed = 0;
    g_passed = 0;

    test_clean_directory_accepted();
    test_version_dll_rejected();
    test_defluxcore_asi_rejected();
    test_defluxcore_dll_rejected();
    test_entry_mode_marker_rejected();
    test_multiple_contaminants_reported_deterministically();
    test_contamination_result_status();
    test_qualify_observation_stops_before_open_process();
    test_supported_build_registry_unchanged();
    test_readiness_remains_not_implemented();
    test_legacy_anchor_trust_label();

    passed += g_passed;
    failed += g_failed;
    return g_failed == 0 ? 0 : 1;
}

} // namespace deflux_tests
