#include "deflux/game/gta_sa_de/adapter.hpp"
#include "deflux/game/gta_sa_de/research_build_allowlist.hpp"
#include "deflux/game/gta_sa_de/research_launcher.hpp"
#include "deflux/game/gta_sa_de/research_remote_loader.hpp"
#include "deflux/runtime/research_harness.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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

#define CHECK_RL(expr) check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

std::wstring sibling_m1_path(const wchar_t* file_name) {
    wchar_t exe[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) {
        return {};
    }
    wchar_t* slash = exe;
    for (wchar_t* p = exe; *p != L'\0'; ++p) {
        if (*p == L'\\' || *p == L'/') {
            slash = p;
        }
    }
    const std::wstring dir(exe, static_cast<std::size_t>(slash - exe + 1));
    return dir + file_name;
}

struct FixtureProcess {
    PROCESS_INFORMATION pi{};
    STARTUPINFOW si{};
    std::wstring command_line_storage;

    ~FixtureProcess() {
        if (pi.hProcess != nullptr) {
            TerminateProcess(pi.hProcess, 0);
            WaitForSingleObject(pi.hProcess, 5000);
        }
        if (pi.hThread != nullptr) {
            CloseHandle(pi.hThread);
        }
        if (pi.hProcess != nullptr) {
            CloseHandle(pi.hProcess);
        }
    }

    bool start(DWORD wait_ms, DWORD settle_ms = 300) {
        const std::wstring fixture = sibling_m1_path(L"DeFlux_ResearchLoader_Fixture.exe");
        command_line_storage = L"\"" + fixture + L"\" --wait-ms " + std::to_wstring(wait_ms);
        si.cb = sizeof(si);
        if (!CreateProcessW(fixture.c_str(),
                            command_line_storage.data(),
                            nullptr,
                            nullptr,
                            FALSE,
                            CREATE_NO_WINDOW,
                            nullptr,
                            nullptr,
                            &si,
                            &pi)) {
            return false;
        }
        if (settle_ms > 0) {
            Sleep(settle_ms);
        }
        return true;
    }
};

bool query_live_fixture_identity(DWORD pid, deflux::core::ProcessInstance& out) {
    return deflux::game::gta_sa_de::query_research_target_identity(
        static_cast<std::uint32_t>(pid), out);
}

void test_injection_disabled_unless_explicit() {
    char arg0[] = "DeFlux_ResearchLauncher.exe";
    char* argv[] = {arg0};
    const auto result = deflux::game::gta_sa_de::run_research_launcher(1, argv);
    CHECK_RL(result.ok);
    CHECK_RL(!result.inject_requested);
    CHECK_RL(result.load_result == deflux::game::gta_sa_de::ResearchLoadResult::LoadNotRequested);
}

void test_arbitrary_override_rejected() {
    char arg0[] = "DeFlux_ResearchLauncher.exe";
    char arg1[] = "--inject";
    char arg2[] = "--pid";
    char arg3[] = "1234";
    char* argv[] = {arg0, arg1, arg2, arg3};
    const auto result = deflux::game::gta_sa_de::run_research_launcher(4, argv);
    CHECK_RL(result.inject_requested);
    CHECK_RL(result.load_result
             == deflux::game::gta_sa_de::ResearchLoadResult::ArbitraryTargetRejected);
}

void test_inject_without_running_target() {
    char arg0[] = "DeFlux_ResearchLauncher.exe";
    char arg1[] = "--inject";
    char* argv[] = {arg0, arg1};
    char isolation[8]{};
    const DWORD isolation_length =
        GetEnvironmentVariableA("DEFLUX_RESEARCH_ISOLATE_LIVE_TARGET", isolation, static_cast<DWORD>(sizeof(isolation)));
    CHECK_RL(isolation_length == 1 && isolation[0] == '1');
    const auto result = deflux::game::gta_sa_de::run_research_launcher(2, argv);
    CHECK_RL(result.inject_requested);
    CHECK_RL(!result.ok);
    CHECK_RL(result.load_result == deflux::game::gta_sa_de::ResearchLoadResult::TargetNotRunning);
    CHECK_RL(std::strcmp(result.message.c_str(), "Live GTA resolution is isolated from automated tests.") == 0);
}

void test_research_sha_and_production_separation() {
    using deflux::game::gta_sa_de::is_production_supported_sha256;
    using deflux::game::gta_sa_de::is_research_allowlisted_sha256;
    using deflux::game::gta_sa_de::kResearchAllowlistedSha256;

    CHECK_RL(is_research_allowlisted_sha256(kResearchAllowlistedSha256));
    CHECK_RL(!is_production_supported_sha256(kResearchAllowlistedSha256));
}

void test_runtime_dll_path_bounds_and_gta_isolation() {
    const std::wstring launcher = sibling_m1_path(L"DeFlux_ResearchLauncher.exe");
    std::wstring runtime_path;
    std::string error;
    CHECK_RL(!launcher.empty());
    CHECK_RL(deflux::game::gta_sa_de::resolve_trusted_runtime_dll_path(
        runtime_path, launcher, L"C:\\Program Files\\GTA", error));
    CHECK_RL(!runtime_path.empty());
    CHECK_RL(deflux::game::gta_sa_de::validate_runtime_dll_outside_gta_directory(
        runtime_path, L"C:\\Program Files\\GTA", error));
}

void test_utf16_path_serialization_and_buffer_size() {
    std::vector<std::uint8_t> bytes;
    std::string error;
    const std::wstring path = L"C:\\DeFlux\\build\\m1\\DeFlux_Runtime.dll";
    CHECK_RL(deflux::game::gta_sa_de::research_remote_path_buffer_bytes(path.size()) > 0);
    CHECK_RL(deflux::game::gta_sa_de::serialize_utf16_le_path(path, bytes, error));
    CHECK_RL(bytes.size() == (path.size() + 1) * sizeof(wchar_t));
}

void test_minimal_process_rights_constants() {
    CHECK_RL(deflux::game::gta_sa_de::kResearchInjectProcessAccess == 0x042Au);
    CHECK_RL(deflux::game::gta_sa_de::kResearchQueryProcessAccess == 0x1000u);
    CHECK_RL(deflux::game::gta_sa_de::kResearchRemoteThreadWaitMs >= 1000u);
}

void test_invalid_runtime_dll_rejected() {
    FixtureProcess fixture{};
    const std::wstring fixture_path = sibling_m1_path(L"DeFlux_ResearchLoader_Fixture.exe");
    if (!fixture.start(30000)) {
        std::printf("SKIP synthetic fixture unavailable\n");
        return;
    }
    deflux::core::ProcessInstance identity{};
    CHECK_RL(query_live_fixture_identity(fixture.pi.dwProcessId, identity));
    const auto diag = deflux::game::gta_sa_de::execute_research_remote_load_synthetic_fixture(
        fixture_path,
        sibling_m1_path(L"DeFlux_Foundation_Tests.exe"),
        L"C:\\DeFlux\\missing\\DeFlux_Runtime.dll",
        identity.process_id,
        identity);
    CHECK_RL(diag.result == deflux::game::gta_sa_de::ResearchLoadResult::RuntimeDllNotFound);
}

void test_synthetic_successful_dll_load() {
    FixtureProcess fixture{};
    const std::wstring fixture_path = sibling_m1_path(L"DeFlux_ResearchLoader_Fixture.exe");
    const std::wstring runtime_path = sibling_m1_path(L"DeFlux_Runtime.dll");
    if (!fixture.start(30000)) {
        std::printf("SKIP synthetic fixture unavailable\n");
        return;
    }
    deflux::core::ProcessInstance identity{};
    CHECK_RL(query_live_fixture_identity(fixture.pi.dwProcessId, identity));
    const auto diag = deflux::game::gta_sa_de::execute_research_remote_load_synthetic_fixture(
        fixture_path,
        sibling_m1_path(L"DeFlux_Foundation_Tests.exe"),
        runtime_path,
        identity.process_id,
        identity);
    CHECK_RL(diag.result == deflux::game::gta_sa_de::ResearchLoadResult::LoadSucceeded);
    CHECK_RL(diag.cleanup_complete);
}

void test_duplicate_synthetic_load() {
    FixtureProcess fixture{};
    const std::wstring fixture_path = sibling_m1_path(L"DeFlux_ResearchLoader_Fixture.exe");
    const std::wstring runtime_path = sibling_m1_path(L"DeFlux_Runtime.dll");
    if (!fixture.start(30000)) {
        std::printf("SKIP synthetic fixture unavailable\n");
        return;
    }
    deflux::core::ProcessInstance identity{};
    CHECK_RL(query_live_fixture_identity(fixture.pi.dwProcessId, identity));
    const auto first = deflux::game::gta_sa_de::execute_research_remote_load_synthetic_fixture(
        fixture_path, sibling_m1_path(L"DeFlux_Foundation_Tests.exe"), runtime_path,
        identity.process_id, identity);
    CHECK_RL(first.result == deflux::game::gta_sa_de::ResearchLoadResult::LoadSucceeded);
    const auto second = deflux::game::gta_sa_de::execute_research_remote_load_synthetic_fixture(
        fixture_path, sibling_m1_path(L"DeFlux_Foundation_Tests.exe"), runtime_path,
        identity.process_id, identity);
    CHECK_RL(second.result == deflux::game::gta_sa_de::ResearchLoadResult::LoadSucceeded);
    CHECK_RL(second.duplicate_load_attempt);
}

void test_duplicate_synthetic_load_stress_100() {
    int ok_count = 0;
    for (int i = 0; i < 100; ++i) {
        FixtureProcess fixture{};
        const std::wstring fixture_path = sibling_m1_path(L"DeFlux_ResearchLoader_Fixture.exe");
        const std::wstring runtime_path = sibling_m1_path(L"DeFlux_Runtime.dll");
        if (!fixture.start(30000)) {
            std::printf("duplicate stress fixture start failed at %d\n", i);
            break;
        }
        deflux::core::ProcessInstance identity{};
        if (!query_live_fixture_identity(fixture.pi.dwProcessId, identity)) {
            std::printf("duplicate stress identity failed at %d\n", i);
            break;
        }
        const auto first = deflux::game::gta_sa_de::execute_research_remote_load_synthetic_fixture(
            fixture_path, sibling_m1_path(L"DeFlux_Foundation_Tests.exe"), runtime_path,
            identity.process_id, identity);
        const auto second = deflux::game::gta_sa_de::execute_research_remote_load_synthetic_fixture(
            fixture_path, sibling_m1_path(L"DeFlux_Foundation_Tests.exe"), runtime_path,
            identity.process_id, identity);
        if (first.result == deflux::game::gta_sa_de::ResearchLoadResult::LoadSucceeded
            && second.result == deflux::game::gta_sa_de::ResearchLoadResult::LoadSucceeded
            && second.duplicate_load_attempt) {
            ++ok_count;
            continue;
        }
        std::printf("duplicate stress fail iter=%d first=%u second=%u dup=%d msg=%s\n",
                    i,
                    static_cast<unsigned>(first.result),
                    static_cast<unsigned>(second.result),
                    second.duplicate_load_attempt ? 1 : 0,
                    second.message.c_str());
        break;
    }
    CHECK_RL(ok_count == 100);
}

void test_process_restart_mismatch_rejected() {
    FixtureProcess fixture{};
    const std::wstring fixture_path = sibling_m1_path(L"DeFlux_ResearchLoader_Fixture.exe");
    const std::wstring runtime_path = sibling_m1_path(L"DeFlux_Runtime.dll");
    if (!fixture.start(30000)) {
        std::printf("SKIP synthetic fixture unavailable\n");
        return;
    }
    deflux::core::ProcessInstance stale{};
    CHECK_RL(query_live_fixture_identity(fixture.pi.dwProcessId, stale));
    stale.creation_identity ^= 0xFULL;
    const auto diag = deflux::game::gta_sa_de::execute_research_remote_load_synthetic_fixture(
        fixture_path, sibling_m1_path(L"DeFlux_Foundation_Tests.exe"), runtime_path,
        stale.process_id, stale);
    CHECK_RL(diag.result == deflux::game::gta_sa_de::ResearchLoadResult::TargetIdentityMismatch);
}

void test_target_exit_before_load() {
    FixtureProcess fixture{};
    const std::wstring fixture_path = sibling_m1_path(L"DeFlux_ResearchLoader_Fixture.exe");
    const std::wstring runtime_path = sibling_m1_path(L"DeFlux_Runtime.dll");
    if (!fixture.start(500, 50)) {
        std::printf("SKIP synthetic fixture unavailable\n");
        return;
    }
    deflux::core::ProcessInstance identity{};
    CHECK_RL(query_live_fixture_identity(fixture.pi.dwProcessId, identity));
    WaitForSingleObject(fixture.pi.hProcess, 5000);
    const auto diag = deflux::game::gta_sa_de::execute_research_remote_load_synthetic_fixture(
        fixture_path, sibling_m1_path(L"DeFlux_Foundation_Tests.exe"), runtime_path,
        identity.process_id, identity);
    CHECK_RL(diag.result == deflux::game::gta_sa_de::ResearchLoadResult::TargetIdentityMismatch
             || diag.result == deflux::game::gta_sa_de::ResearchLoadResult::OpenProcessFailed);
    (void)runtime_path;
}

bool wait_for_runtime_bootstrap(HMODULE module, DWORD timeout_ms) {
    const auto complete_fn = reinterpret_cast<int32_t (*)(void)>(
        GetProcAddress(module, "deflux_runtime_research_bootstrap_complete"));
    if (complete_fn == nullptr) {
        return false;
    }
    const DWORD deadline = GetTickCount() + timeout_ms;
    while (GetTickCount() < deadline) {
        if (complete_fn() != 0) {
            return true;
        }
        Sleep(10);
    }
    return complete_fn() != 0;
}

void test_duplicate_load_and_no_auto_research() {
    const std::wstring runtime_path = sibling_m1_path(L"DeFlux_Runtime.dll");
    HMODULE module = LoadLibraryW(runtime_path.c_str());
    CHECK_RL(module != nullptr);
    if (module != nullptr) {
        CHECK_RL(wait_for_runtime_bootstrap(module, 5000));
        const auto substate_fn = reinterpret_cast<int32_t (*)(void)>(
            GetProcAddress(module, "deflux_runtime_research_substate"));
        CHECK_RL(substate_fn != nullptr);
        if (substate_fn != nullptr) {
            CHECK_RL(substate_fn() == 0);
        }
        // Do not FreeLibrary: deferred CRT workers may still be draining. Process
        // teardown reclaims the module. Leaving it loaded must not auto-arm research.
    }
}

void test_readiness_and_registry_unchanged() {
    using deflux::game::gta_sa_de::GtaSaDeAdapter;
    using deflux::game::Readiness;

    const GtaSaDeAdapter adapter;
    CHECK_RL(adapter.engine_ready().status == Readiness::NotImplemented);
    CHECK_RL(adapter.world_ready().status == Readiness::NotImplemented);
    CHECK_RL(adapter.local_player_ready().status == Readiness::NotImplemented);
    CHECK_RL(adapter.probe_supported_game().status != deflux::game::ProbeStatus::Supported);
}

void test_runtime_abi_sizes_unchanged() {
    CHECK_RL(DEFLUX_RUNTIME_ABI_VERSION == 1u);
    CHECK_RL(sizeof(DefluxRuntimeInitContext) == 12);
}

} // namespace

int run_qualification_research_remote_loader_tests(int& passed, int& failed) {
    test_injection_disabled_unless_explicit();
    test_arbitrary_override_rejected();
    test_inject_without_running_target();
    test_research_sha_and_production_separation();
    test_runtime_dll_path_bounds_and_gta_isolation();
    test_utf16_path_serialization_and_buffer_size();
    test_minimal_process_rights_constants();
    test_invalid_runtime_dll_rejected();
    test_synthetic_successful_dll_load();
    test_duplicate_synthetic_load();
    test_duplicate_synthetic_load_stress_100();
    test_process_restart_mismatch_rejected();
    test_target_exit_before_load();
    test_duplicate_load_and_no_auto_research();
    test_readiness_and_registry_unchanged();
    test_runtime_abi_sizes_unchanged();
    passed += g_passed;
    failed += g_failed;
    return g_failed == 0 ? 0 : 1;
}

} // namespace deflux_tests
