#include "deflux/core/process_lifecycle.hpp"
#include "deflux/game/gta_sa_de/adapter.hpp"
#include "deflux/game/gta_sa_de/detection.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace deflux_tests {
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

#define CHECK_DET(expr) check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

constexpr const char kHelloSha256[] =
    "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";

void test_compatibility_policy() {
    using deflux::game::gta_sa_de::Compatibility;
    using deflux::game::gta_sa_de::InstallationStatus;
    using deflux::game::gta_sa_de::compatibility_of;
    using deflux::game::gta_sa_de::to_probe_status;
    using deflux::game::ProbeStatus;

    CHECK_DET(compatibility_of(InstallationStatus::NotFound, false, false) == Compatibility::NotFound);
    CHECK_DET(compatibility_of(InstallationStatus::NotFound, true, true) == Compatibility::NotFound);
    CHECK_DET(compatibility_of(InstallationStatus::Error, true, true) == Compatibility::Error);
    CHECK_DET(compatibility_of(InstallationStatus::Found, false, true) == Compatibility::Error);
    CHECK_DET(compatibility_of(InstallationStatus::Found, true, false)
              == Compatibility::DetectedButUnsupported);
    CHECK_DET(compatibility_of(InstallationStatus::Found, true, true) == Compatibility::Supported);

    CHECK_DET(to_probe_status(Compatibility::Supported) == ProbeStatus::Supported);
    CHECK_DET(to_probe_status(Compatibility::DetectedButUnsupported) == ProbeStatus::Unsupported);
    CHECK_DET(to_probe_status(Compatibility::NotFound) == ProbeStatus::Unavailable);
    CHECK_DET(to_probe_status(Compatibility::Error) == ProbeStatus::Failed);

    CHECK_DET(!deflux::game::gta_sa_de::is_supported_fingerprint(kHelloSha256));
    CHECK_DET(!deflux::game::gta_sa_de::is_supported_fingerprint("not-a-hash"));
    CHECK_DET(!deflux::game::gta_sa_de::is_supported_fingerprint(""));
}

void test_explicit_paths() {
    using deflux::game::gta_sa_de::Compatibility;
    using deflux::game::gta_sa_de::GtaSaDeAdapter;
    using deflux::game::gta_sa_de::InstallationStatus;

    const GtaSaDeAdapter adapter;

    const auto missing_empty = adapter.detect_from_executable("");
    CHECK_DET(missing_empty.installation == InstallationStatus::NotFound);
    CHECK_DET(missing_empty.compatibility == Compatibility::NotFound);
    CHECK_DET(!missing_empty.running);
    CHECK_DET(missing_empty.process_id == 0);
    CHECK_DET(missing_empty.sha256_hex.empty());
    CHECK_DET(!deflux::game::succeeded(deflux::game::gta_sa_de::to_probe_status(missing_empty.compatibility)));

    const auto missing = adapter.detect_from_executable(
        "Z:/deflux_task3_no_such_file/nope.exe");
    CHECK_DET(missing.installation == InstallationStatus::NotFound);
    CHECK_DET(missing.compatibility == Compatibility::NotFound);
    CHECK_DET(!missing.running);
    CHECK_DET(missing.process_id == 0);
    CHECK_DET(missing.sha256_hex.empty());

    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "deflux_m1_task3";
    std::error_code ec;
    fs::create_directories(dir, ec);
    CHECK_DET(!ec);

    const auto directory_snap = adapter.detect_from_executable(dir.string());
    CHECK_DET(directory_snap.installation == InstallationStatus::Error);
    CHECK_DET(directory_snap.compatibility == Compatibility::Error);
    CHECK_DET(!deflux::game::succeeded(
        deflux::game::gta_sa_de::to_probe_status(directory_snap.compatibility)));

    const fs::path hello = dir / "hello.bin";
    {
        std::ofstream out(hello, std::ios::binary | std::ios::trunc);
        CHECK_DET(static_cast<bool>(out));
        out.write("hello", 5);
    }

    const auto hashed = adapter.detect_from_executable(hello.string());
    CHECK_DET(hashed.installation == InstallationStatus::Found);
    CHECK_DET(hashed.compatibility == Compatibility::DetectedButUnsupported);
    CHECK_DET(hashed.sha256_hex == kHelloSha256);
    CHECK_DET(hashed.file_size_bytes == 5);
    CHECK_DET(!hashed.running);
    CHECK_DET(hashed.process_id == 0);
    CHECK_DET(!deflux::game::gta_sa_de::is_supported_fingerprint(hashed.sha256_hex));
    CHECK_DET(!deflux::game::succeeded(
        deflux::game::gta_sa_de::to_probe_status(hashed.compatibility)));

    const fs::path hello2 = dir / "hello2.bin";
    {
        std::ofstream out(hello2, std::ios::binary | std::ios::trunc);
        CHECK_DET(static_cast<bool>(out));
        out.write("hello", 5);
    }
    const auto hashed2 = adapter.detect_from_executable(hello2.string());
    CHECK_DET(hashed2.sha256_hex == hashed.sha256_hex);

    const fs::path other = dir / "other.bin";
    {
        std::ofstream out(other, std::ios::binary | std::ios::trunc);
        CHECK_DET(static_cast<bool>(out));
        out.write("world", 5);
    }
    const auto hashed_other = adapter.detect_from_executable(other.string());
    CHECK_DET(hashed_other.sha256_hex != hashed.sha256_hex);
    CHECK_DET(hashed_other.compatibility == Compatibility::DetectedButUnsupported);

    fs::remove(hello, ec);
    fs::remove(hello2, ec);
    fs::remove(other, ec);
    fs::remove(dir, ec);
}

void test_environment_detect_never_succeeds_unregistered() {
    using deflux::game::gta_sa_de::Compatibility;
    using deflux::game::gta_sa_de::GtaSaDeAdapter;

    const GtaSaDeAdapter adapter;
    const auto snap = adapter.detect();
    CHECK_DET(snap.compatibility != Compatibility::Supported);
    CHECK_DET(!deflux::game::succeeded(adapter.probe_supported_game()));
    CHECK_DET(!deflux::game::succeeded(adapter.detected_game_build()));
    CHECK_DET(std::strcmp(deflux::game::gta_sa_de::to_string(snap.installation), "FOUND") == 0
              || std::strcmp(deflux::game::gta_sa_de::to_string(snap.installation), "NOT_FOUND") == 0
              || std::strcmp(deflux::game::gta_sa_de::to_string(snap.installation), "ERROR") == 0);
    if (!snap.running) {
        CHECK_DET(snap.process_id == 0);
    } else {
        CHECK_DET(snap.process_id != 0);
        CHECK_DET(!snap.executable_path.empty());
    }
}

void test_path_matching_and_observation_boundaries() {
    using deflux::core::ProcessLifecycle;
    using deflux::game::gta_sa_de::Compatibility;
    using deflux::game::gta_sa_de::GtaSaDeAdapter;
    using deflux::game::gta_sa_de::executable_paths_match;

    CHECK_DET(executable_paths_match("C:/Game/SanAndreas.exe", "C:\\Game\\SanAndreas.exe"));
    CHECK_DET(executable_paths_match("C:\\Game\\SanAndreas.exe", "c:\\game\\sanandreas.exe"));
    CHECK_DET(!executable_paths_match("C:\\Game\\SanAndreas.exe", "C:\\Other\\SanAndreas.exe"));
    CHECK_DET(!executable_paths_match("", "C:\\Game\\SanAndreas.exe"));
    CHECK_DET(!executable_paths_match("C:\\Game\\SanAndreas.exe", ""));

    GtaSaDeAdapter adapter;
    const auto first = adapter.observe();
    CHECK_DET(first.state == ProcessLifecycle::NotRunning
              || first.state == ProcessLifecycle::ProcessDetected
              || first.state == ProcessLifecycle::ObservationError);
    CHECK_DET(first.state != ProcessLifecycle::ProcessExited);

    if (first.state == ProcessLifecycle::NotRunning) {
        CHECK_DET(first.instance.process_id == 0);
        CHECK_DET(first.instance.creation_identity == 0);
        const auto second = adapter.observe();
        CHECK_DET(second.state == ProcessLifecycle::NotRunning
                  || second.state == ProcessLifecycle::ProcessDetected
                  || second.state == ProcessLifecycle::ObservationError);
        if (second.state == ProcessLifecycle::NotRunning) {
            CHECK_DET(!second.instance_changed);
        }
    }

    if (first.state == ProcessLifecycle::ProcessDetected) {
        CHECK_DET(deflux::core::is_valid_process_instance(first.instance));
        const auto second = adapter.observe();
        if (second.state == ProcessLifecycle::ProcessDetected
            && deflux::core::same_process_instance(first.instance, second.instance)) {
            CHECK_DET(!second.instance_changed);
        }
    }

    const auto snap = adapter.detect();
    CHECK_DET(snap.compatibility != Compatibility::Supported);
    CHECK_DET(adapter.engine_ready().status == deflux::game::Readiness::NotImplemented);
    CHECK_DET(adapter.world_ready().status == deflux::game::Readiness::NotImplemented);
    CHECK_DET(adapter.local_player_ready().status == deflux::game::Readiness::NotImplemented);
    CHECK_DET(!deflux::game::succeeded(adapter.engine_ready()));
}

} // namespace

int run_gta_sa_de_detection_tests(int& passed, int& failed) {
    const int passed_before = g_passed;
    const int failed_before = g_failed;
    test_compatibility_policy();
    test_explicit_paths();
    test_environment_detect_never_succeeds_unregistered();
    test_path_matching_and_observation_boundaries();
    passed += (g_passed - passed_before);
    failed += (g_failed - failed_before);
    return g_failed - failed_before;
}

} // namespace deflux_tests
