#include "deflux/core/process_lifecycle.hpp"
#include "deflux/game/gta_sa_de/adapter.hpp"
#include "deflux/game/gta_sa_de/in_process_research.hpp"
#include "deflux/game/gta_sa_de/research_build_allowlist.hpp"
#include "deflux/game/gta_sa_de/research_evidence_log.hpp"
#include "deflux/game/gta_sa_de/research_intent.hpp"
#include "deflux/game/gta_sa_de/research_activation_status.hpp"
#include "deflux/game/gta_sa_de/research_remote_loader.hpp"
#include "deflux/game/gta_sa_de/research_synthetic_activation.hpp"
#include "deflux/runtime/abi.h"
#include "deflux/runtime/research_harness.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <ShlObj.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <sstream>
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

#define CHECK_RA(expr) check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

void copy_test_field(char* dest, std::size_t capacity, const char* src) {
    std::memset(dest, 0, capacity);
    if (src == nullptr || capacity == 0) {
        return;
    }
    const std::size_t length = std::strlen(src);
    const std::size_t copy = length < (capacity - 1) ? length : (capacity - 1);
    if (copy > 0) {
        std::memcpy(dest, src, copy);
    }
}

std::uint64_t steady_ms() {
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch())
            .count());
}

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

std::string temp_evidence_directory() {
    wchar_t temp[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, temp) == 0) {
        return "C:\\Temp\\DeFluxResearchEvidence";
    }
    std::string dir;
    const int needed = WideCharToMultiByte(
        CP_UTF8, 0, temp, -1, nullptr, 0, nullptr, nullptr);
    if (needed > 1) {
        dir.resize(static_cast<std::size_t>(needed - 1));
        WideCharToMultiByte(CP_UTF8, 0, temp, -1, dir.data(), needed, nullptr, nullptr);
    }
    if (!dir.empty() && dir.back() != '\\' && dir.back() != '/') {
        dir.push_back('\\');
    }
    dir += "DeFlux_9B_Evidence\\";
    wchar_t wide[MAX_PATH]{};
    MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, wide, MAX_PATH);
    CreateDirectoryW(wide, nullptr);
    return dir;
}

deflux::game::gta_sa_de::ResearchIntentRecord make_valid_synthetic_record(
    deflux::core::ProcessInstance target) {
    using namespace deflux::game::gta_sa_de;
    if (target.process_id == 0 || target.creation_identity == 0 || target.executable_path.empty()) {
        target.process_id = 4242;
        target.creation_identity = 777;
        target.executable_path = "C:\\x\\DeFlux_ResearchLoader_Fixture.exe";
    }
    ResearchIntentRecord record{};
    record.magic = kResearchIntentMagicSynthetic;
    record.schema_version = kResearchIntentSchemaVersion;
    record.state = static_cast<std::uint32_t>(ResearchIntentState::Pending);
    record.target_process_id = target.process_id;
    record.target_creation_identity = target.creation_identity;
    record.session_nonce = 0xA11CEu;
    record.experiment_version = kGGameStateInProcessExperimentVersion;
    record.capability = static_cast<std::uint32_t>(ResearchCapability::PassiveRead);
    record.interval_ms = 50;
    record.max_samples = 3;
    record.published_at_ms = steady_ms();
    copy_test_field(record.experiment_id,
                    sizeof(record.experiment_id),
                    kGGameStateInProcessExperimentId);
    copy_test_field(record.expected_sha256,
                    sizeof(record.expected_sha256),
                    kResearchAllowlistedSha256);
    return record;
}

std::wstring build_child_environment_block(const std::string& evidence_dir_utf8,
                                           bool synthetic_async) {
    std::wstring block;
    if (wchar_t* current = GetEnvironmentStringsW()) {
        for (const wchar_t* cursor = current; *cursor != L'\0';) {
            // Skip inherited async flag so the test explicitly controls it.
            if (wcsncmp(cursor, L"DEFLUX_RESEARCH_SYNTHETIC_ASYNC=", 32) != 0
                && wcsncmp(cursor, L"DEFLUX_RESEARCH_EVIDENCE_DIR=", 28) != 0) {
                block.append(cursor);
                block.push_back(L'\0');
            }
            cursor += wcslen(cursor) + 1;
        }
        FreeEnvironmentStringsW(current);
    }
    if (!evidence_dir_utf8.empty()) {
        const std::string entry = "DEFLUX_RESEARCH_EVIDENCE_DIR=" + evidence_dir_utf8;
        wchar_t wide[1024]{};
        MultiByteToWideChar(CP_UTF8, 0, entry.c_str(), -1, wide, 1024);
        block.append(wide);
        block.push_back(L'\0');
    }
    if (synthetic_async) {
        block.append(L"DEFLUX_RESEARCH_SYNTHETIC_ASYNC=1");
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

struct FixtureProcess {
    PROCESS_INFORMATION pi{};
    STARTUPINFOW si{};
    std::wstring command_line_storage;
    std::wstring environment_block_storage;

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

    bool alive() const {
        if (pi.hProcess == nullptr) {
            return false;
        }
        DWORD code = STILL_ACTIVE;
        if (GetExitCodeProcess(pi.hProcess, &code) == FALSE) {
            return false;
        }
        return code == STILL_ACTIVE;
    }

    bool start(DWORD wait_ms = 30000,
               const std::string& evidence_dir_utf8 = {},
               bool synthetic_async = false) {
        const std::wstring fixture = sibling_m1_path(L"DeFlux_ResearchLoader_Fixture.exe");
        command_line_storage = L"\"" + fixture + L"\" --wait-ms " + std::to_wstring(wait_ms);
        si.cb = sizeof(si);
        wchar_t* env = nullptr;
        if (!evidence_dir_utf8.empty() || synthetic_async) {
            environment_block_storage =
                build_child_environment_block(evidence_dir_utf8, synthetic_async);
            env = environment_block_storage.data();
        }
        if (!CreateProcessW(fixture.c_str(),
                            command_line_storage.data(),
                            nullptr,
                            nullptr,
                            FALSE,
                            CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                            env,
                            nullptr,
                            &si,
                            &pi)) {
            return false;
        }
        Sleep(200);
        return alive();
    }
};

bool publish_synthetic_intent(const deflux::core::ProcessInstance& target,
                            std::uint32_t interval_ms,
                            std::uint32_t max_samples,
                            deflux::game::gta_sa_de::ResearchIntentSession& session) {
    using namespace deflux::game::gta_sa_de;
    ResearchIntentPublishRequest request{};
    request.target = target;
    request.synthetic_fixture = true;
    request.interval_ms = interval_ms;
    request.max_samples = max_samples;
    request.session_nonce = 0xA11CEu;
    std::string message;
    return publish_research_intent_session(session, request, message);
}

bool wait_for_runtime_bootstrap(HMODULE module, std::uint32_t timeout_ms) {
    const auto complete_fn = reinterpret_cast<int32_t (*)(void)>(
        GetProcAddress(module, "deflux_runtime_research_bootstrap_complete"));
    if (complete_fn == nullptr) {
        return false;
    }
    const std::uint64_t deadline = steady_ms() + timeout_ms;
    while (steady_ms() < deadline) {
        if (complete_fn() != 0) {
            return true;
        }
        Sleep(10);
    }
    return complete_fn() != 0;
}

bool wait_for_substate(int32_t expected, std::uint32_t timeout_ms) {
    const std::uint64_t deadline = steady_ms() + timeout_ms;
    while (steady_ms() < deadline) {
        const std::wstring runtime_path = sibling_m1_path(L"DeFlux_Runtime.dll");
        HMODULE module = GetModuleHandleW(runtime_path.c_str());
        if (module == nullptr) {
            module = LoadLibraryW(runtime_path.c_str());
        }
        if (module != nullptr) {
            const auto substate_fn = reinterpret_cast<int32_t (*)(void)>(
                GetProcAddress(module, "deflux_runtime_research_substate"));
            if (substate_fn != nullptr && substate_fn() == expected) {
                return true;
            }
        }
        Sleep(25);
    }
    return false;
}

int32_t query_runtime_substate_in_fixture(DWORD pid) {
    (void)pid;
    const std::wstring runtime_path = sibling_m1_path(L"DeFlux_Runtime.dll");
    HMODULE module = GetModuleHandleW(runtime_path.c_str());
    if (module == nullptr) {
        return -1;
    }
    const auto substate_fn = reinterpret_cast<int32_t (*)(void)>(
        GetProcAddress(module, "deflux_runtime_research_substate"));
    return substate_fn != nullptr ? substate_fn() : -1;
}

void test_no_intent_record_validation_disabled() {
    using namespace deflux::game::gta_sa_de;
    ResearchIntentRecord record{};
    std::string reason;
    CHECK_RA(!validate_research_intent_record(record, true, reason));
}

void test_malformed_intent_rejected() {
    using namespace deflux::game::gta_sa_de;
    ResearchIntentRecord record = make_valid_synthetic_record({});
    record.magic = 0;
    std::string reason;
    CHECK_RA(!validate_research_intent_record(record, true, reason));
}

void test_stale_pid_rejected() {
    using namespace deflux::game::gta_sa_de;
    deflux::core::ProcessInstance target{};
    target.process_id = GetCurrentProcessId();
    target.creation_identity = 1;
    target.executable_path = "C:\\fixture\\DeFlux_ResearchLoader_Fixture.exe";
    ResearchIntentRecord record = make_valid_synthetic_record(target);
    record.target_process_id ^= 0xFFFFu;
    std::string reason;
    CHECK_RA(validate_research_intent_record(record, true, reason));
    CHECK_RA(!research_intent_matches_process(record,
                                              target.process_id,
                                              target.creation_identity,
                                              target.executable_path.c_str()));
}

void test_stale_creation_identity_rejected() {
    using namespace deflux::game::gta_sa_de;
    deflux::core::ProcessInstance target{};
    target.process_id = GetCurrentProcessId();
    target.creation_identity = 1;
    target.executable_path = "C:\\fixture\\DeFlux_ResearchLoader_Fixture.exe";
    ResearchIntentRecord record = make_valid_synthetic_record(target);
    record.target_creation_identity ^= 0xFULL;
    std::string reason;
    CHECK_RA(validate_research_intent_record(record, true, reason));
    CHECK_RA(!research_intent_matches_process(record,
                                              target.process_id,
                                              target.creation_identity,
                                              target.executable_path.c_str()));
}

void test_wrong_sha_rejected() {
    using namespace deflux::game::gta_sa_de;
    ResearchIntentRecord record = make_valid_synthetic_record({});
    copy_test_field(record.expected_sha256, sizeof(record.expected_sha256), "deadbeef");
    std::string reason;
    CHECK_RA(!validate_research_intent_record(record, true, reason));
}

void test_wrong_experiment_rejected() {
    using namespace deflux::game::gta_sa_de;
    ResearchIntentRecord record = make_valid_synthetic_record({});
    copy_test_field(record.experiment_id, sizeof(record.experiment_id), "OTHER_EXPERIMENT");
    std::string reason;
    CHECK_RA(!validate_research_intent_record(record, true, reason));
}

void test_wrong_version_rejected() {
    using namespace deflux::game::gta_sa_de;
    ResearchIntentRecord record = make_valid_synthetic_record({});
    record.experiment_version = 99;
    std::string reason;
    CHECK_RA(!validate_research_intent_record(record, true, reason));
}

void test_wrong_capability_rejected() {
    using namespace deflux::game::gta_sa_de;
    ResearchIntentRecord record = make_valid_synthetic_record({});
    record.capability = static_cast<std::uint32_t>(ResearchCapability::Write);
    std::string reason;
    CHECK_RA(!validate_research_intent_record(record, true, reason));
}

void test_excessive_bounds_clamped_or_rejected() {
    using namespace deflux::game::gta_sa_de;
    ResearchIntentPublishRequest request{};
    request.target.process_id = 1;
    request.target.creation_identity = 1;
    request.target.executable_path = "C:\\x\\DeFlux_ResearchLoader_Fixture.exe";
    request.interval_ms = 1;
    request.max_samples = 9999;
    request.synthetic_fixture = true;
    std::string reason;
    CHECK_RA(!normalize_research_intent_publish_request(request, reason));
    request.interval_ms = kInProcessResearchMaxIntervalMs + 1000;
    request.max_samples = 10;
    CHECK_RA(normalize_research_intent_publish_request(request, reason));
    CHECK_RA(request.interval_ms == kInProcessResearchMaxIntervalMs);
    request.interval_ms = 500;
    request.max_samples = kInProcessResearchMaxSamplesCap + 50;
    CHECK_RA(normalize_research_intent_publish_request(request, reason));
    CHECK_RA(request.max_samples == kInProcessResearchMaxSamplesCap);
}

void test_valid_synthetic_intent_accepted() {
    using namespace deflux::game::gta_sa_de;
    ResearchIntentRecord record = make_valid_synthetic_record({});
    std::string reason;
    CHECK_RA(validate_research_intent_record(record, true, reason));
}

void test_runtime_revalidates_intent_fields() {
    using namespace deflux::game::gta_sa_de;
    ResearchIntentRecord record = make_valid_synthetic_record({});
    record.capability = static_cast<std::uint32_t>(ResearchCapability::Hook);
    std::string reason;
    CHECK_RA(!validate_research_intent_record(record, true, reason));
}

void test_intent_process_lifetime_scoped() {
    using namespace deflux::game::gta_sa_de;
    ResearchIntentSession session{};
    ResearchIntentPublishRequest request{};
    request.target.process_id = 4242;
    request.target.creation_identity = 777;
    request.target.executable_path = "C:\\x\\DeFlux_ResearchLoader_Fixture.exe";
    request.synthetic_fixture = true;
    std::string message;
    CHECK_RA(publish_research_intent_session(session, request, message));
    CHECK_RA(update_research_intent_mapping_state(ResearchIntentState::Pending,
                                                  ResearchIntentState::Consumed,
                                                  message));
    CHECK_RA(!update_research_intent_mapping_state(ResearchIntentState::Pending,
                                                   ResearchIntentState::Consumed,
                                                   message));
    close_research_intent_session(session);
}

void test_intent_cleanup_after_publish() {
    using namespace deflux::game::gta_sa_de;
    ResearchIntentSession session{};
    ResearchIntentPublishRequest request{};
    request.target.process_id = 1;
    request.target.creation_identity = 1;
    request.target.executable_path = "C:\\x\\DeFlux_ResearchLoader_Fixture.exe";
    request.synthetic_fixture = true;
    std::string message;
    CHECK_RA(publish_research_intent_session(session, request, message));
    close_research_intent_session(session);
    ResearchIntentRecord record{};
    bool found = false;
    CHECK_RA(try_open_research_intent_record(record, found, message));
    CHECK_RA(!found);
}

void test_runtime_load_without_intent_no_experiment() {
    using namespace deflux::game::gta_sa_de;
    reset_research_intent_mapping_for_tests();
    ResearchIntentRecord record{};
    bool found = false;
    std::string message;
    CHECK_RA(try_open_research_intent_record(record, found, message));
    CHECK_RA(!found);
}

void test_evidence_log_begin_session_no_deadlock_regression() {
    deflux::game::gta_sa_de::research_evidence_set_output_directory_override(
        temp_evidence_directory());
    deflux::game::gta_sa_de::research_evidence_set_hook_enabled(true);
    deflux::game::gta_sa_de::ResearchEvidenceSessionHeader header{};
    header.process_id = 1;
    header.creation_identity = 1;
    header.session_nonce = 0xDEAD;
    header.experiment_id = deflux::game::gta_sa_de::kGGameStateInProcessExperimentId;
    header.experiment_version = 1;
    header.expected_sha256 = deflux::game::gta_sa_de::kResearchAllowlistedSha256;
    header.observed_sha256 = deflux::game::gta_sa_de::kResearchAllowlistedSha256;
    header.pipeline_proof = true;
    std::string log_path;
    CHECK_RA(deflux::game::gta_sa_de::research_evidence_begin_session(header, log_path));
    deflux::game::gta_sa_de::research_evidence_end_session(
        deflux::game::gta_sa_de::ResearchSubstate::Stopped,
        deflux::game::gta_sa_de::InProcessSignalClassification::None,
        "deadlock regression");
    deflux::game::gta_sa_de::research_evidence_clear_output_directory_override();
    deflux::game::gta_sa_de::delete_file_if_exists_utf8(log_path);
}

void test_synthetic_end_to_end_activation_chain() {
    using namespace deflux::game::gta_sa_de;
    const std::string evidence_dir = temp_evidence_directory();
    // Unique nonce per run so stale logs from prior failures cannot be selected.
    const std::uint64_t session_nonce =
        0xE2E000000ull | (steady_ms() & 0xFFFFFFull);
    FixtureProcess fixture{};
    const std::wstring fixture_path = sibling_m1_path(L"DeFlux_ResearchLoader_Fixture.exe");
    const std::wstring runtime_path = sibling_m1_path(L"DeFlux_Runtime.dll");
    if (!fixture.start(120000, evidence_dir)) {
        std::printf("SKIP synthetic fixture unavailable\n");
        return;
    }
    deflux::core::ProcessInstance identity{};
    CHECK_RA(query_research_target_identity(fixture.pi.dwProcessId, identity));
    SyntheticActivationE2EOptions options{};
    options.fixture_exe_path = fixture_path;
    options.launcher_module_path = sibling_m1_path(L"DeFlux_Foundation_Tests.exe");
    options.runtime_dll_path = runtime_path;
    options.evidence_directory_utf8 = evidence_dir;
    options.target = identity;
    options.interval_ms = 50;
    options.max_samples = 5;
    options.session_nonce = session_nonce;
    const SyntheticActivationE2EResult result = execute_synthetic_activation_e2e(options);
    if (!result.ok) {
        std::printf("E2E diagnostic: %s load=%u intent=%d bootstrap=%d substate=%u samples=%u obs=%d pipe=%d log=%s\n",
                    result.message.c_str(),
                    static_cast<unsigned>(result.load_result),
                    result.intent_consumed ? 1 : 0,
                    result.bootstrap_complete ? 1 : 0,
                    result.final_substate,
                    result.samples_collected,
                    result.observation_complete ? 1 : 0,
                    result.pipeline_proof ? 1 : 0,
                    result.evidence_log_path.c_str());
    }
    CHECK_RA(result.ok);
    CHECK_RA(result.load_result == ResearchLoadResult::LoadSucceeded);
    CHECK_RA(result.intent_consumed);
    CHECK_RA(result.bootstrap_complete);
    CHECK_RA(result.final_substate == static_cast<std::uint32_t>(ResearchSubstate::Stopped));
    CHECK_RA(result.samples_collected == options.max_samples);
    CHECK_RA(result.observation_complete);
    CHECK_RA(result.pipeline_proof);
    CHECK_RA(!result.evidence_log_path.empty());
    CHECK_RA(evidence_log_contains_required_fields(result.evidence_log_path,
                                                   options.max_samples,
                                                   true));
    delete_file_if_exists_utf8(result.evidence_log_path);
}

void test_synthetic_remote_load_without_intent_disabled() {
    using namespace deflux::game::gta_sa_de;
    FixtureProcess fixture{};
    if (!fixture.start(120000)) {
        std::printf("SKIP synthetic fixture unavailable\n");
        return;
    }
    deflux::core::ProcessInstance identity{};
    CHECK_RA(query_research_target_identity(fixture.pi.dwProcessId, identity));
    SyntheticActivationE2EOptions options{};
    options.fixture_exe_path = sibling_m1_path(L"DeFlux_ResearchLoader_Fixture.exe");
    options.launcher_module_path = sibling_m1_path(L"DeFlux_Foundation_Tests.exe");
    options.runtime_dll_path = sibling_m1_path(L"DeFlux_Runtime.dll");
    options.target = identity;
    options.publish_intent = false;
    options.expect_activation = false;
    const SyntheticActivationE2EResult result = execute_synthetic_activation_e2e(options);
    CHECK_RA(result.ok);
    CHECK_RA(result.bootstrap_complete);
    CHECK_RA(result.final_substate == static_cast<std::uint32_t>(ResearchSubstate::Disabled));
}

void test_synthetic_wrong_intent_rejected() {
    using namespace deflux::game::gta_sa_de;
    FixtureProcess fixture{};
    if (!fixture.start(120000)) {
        std::printf("SKIP synthetic fixture unavailable\n");
        return;
    }
    deflux::core::ProcessInstance identity{};
    CHECK_RA(query_research_target_identity(fixture.pi.dwProcessId, identity));
    SyntheticActivationE2EOptions options{};
    options.fixture_exe_path = sibling_m1_path(L"DeFlux_ResearchLoader_Fixture.exe");
    options.launcher_module_path = sibling_m1_path(L"DeFlux_Foundation_Tests.exe");
    options.runtime_dll_path = sibling_m1_path(L"DeFlux_Runtime.dll");
    options.target = identity;
    options.use_intent_override = true;
    options.expect_activation = false;
    options.intent_override.target = identity;
    options.intent_override.target.process_id ^= 0xFFFFu;
    options.intent_override.synthetic_fixture = true;
    options.intent_override.interval_ms = 50;
    options.intent_override.max_samples = 3;
    const SyntheticActivationE2EResult result = execute_synthetic_activation_e2e(options);
    CHECK_RA(result.ok);
    CHECK_RA(!result.intent_consumed);
    CHECK_RA(result.final_substate == static_cast<std::uint32_t>(ResearchSubstate::Disabled));
}

bool evidence_has_breadcrumb(const std::string& log_path, const char* event) {
    std::ifstream input(log_path);
    if (!input.is_open()) {
        return false;
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    return buffer.str().find(event) != std::string::npos;
}

bool lifecycle_has_breadcrumb(const std::string& evidence_dir,
                              std::uint32_t pid,
                              const char* event) {
    std::string path = evidence_dir;
    if (!path.empty() && path.back() != '\\' && path.back() != '/') {
        path.push_back('\\');
    }
    path += "lifecycle_" + std::to_string(pid) + ".log";
    return evidence_has_breadcrumb(path, event);
}

void test_synthetic_async_end_to_end_activation_chain() {
    using namespace deflux::game::gta_sa_de;
    const std::string evidence_dir = temp_evidence_directory();
    const std::uint64_t session_nonce = 0xA5E000000ull | (steady_ms() & 0xFFFFFFull);
    FixtureProcess fixture{};
    if (!fixture.start(120000, evidence_dir, true)) {
        std::printf("SKIP synthetic async fixture unavailable\n");
        return;
    }
    CHECK_RA(fixture.alive());
    deflux::core::ProcessInstance identity{};
    CHECK_RA(query_research_target_identity(fixture.pi.dwProcessId, identity));
    SyntheticActivationE2EOptions options{};
    options.fixture_exe_path = sibling_m1_path(L"DeFlux_ResearchLoader_Fixture.exe");
    options.launcher_module_path = sibling_m1_path(L"DeFlux_Foundation_Tests.exe");
    options.runtime_dll_path = sibling_m1_path(L"DeFlux_Runtime.dll");
    options.evidence_directory_utf8 = evidence_dir;
    options.target = identity;
    options.interval_ms = 50;
    options.max_samples = 10;
    options.session_nonce = session_nonce;
    options.use_async_sampler = true;
    options.wait_timeout_ms = 30000;
    const SyntheticActivationE2EResult result = execute_synthetic_activation_e2e(options);
    if (!result.ok) {
        std::printf("ASYNC E2E diagnostic: %s load=%u intent=%d bootstrap=%d substate=%u samples=%u obs=%d log=%s\n",
                    result.message.c_str(),
                    static_cast<unsigned>(result.load_result),
                    result.intent_consumed ? 1 : 0,
                    result.bootstrap_complete ? 1 : 0,
                    result.final_substate,
                    result.samples_collected,
                    result.observation_complete ? 1 : 0,
                    result.evidence_log_path.c_str());
    }
    CHECK_RA(result.ok);
    CHECK_RA(result.load_result == ResearchLoadResult::LoadSucceeded);
    CHECK_RA(result.intent_consumed);
    CHECK_RA(result.bootstrap_complete);
    CHECK_RA(result.final_substate == static_cast<std::uint32_t>(ResearchSubstate::Stopped));
    CHECK_RA(result.samples_collected == options.max_samples);
    CHECK_RA(result.observation_complete);
    CHECK_RA(!result.evidence_log_path.empty());
    CHECK_RA(evidence_log_contains_required_fields(result.evidence_log_path,
                                                   options.max_samples,
                                                   true));
    CHECK_RA(evidence_has_breadcrumb(result.evidence_log_path, "SAMPLER_THREAD_ENTER"));
    CHECK_RA(evidence_has_breadcrumb(result.evidence_log_path, "SAMPLE_WAIT_BEGIN"));
    CHECK_RA(evidence_has_breadcrumb(result.evidence_log_path, "SAMPLER_STOPPED"));
    CHECK_RA(lifecycle_has_breadcrumb(evidence_dir, identity.process_id, "SAMPLER_THREAD_EXIT"));
    CHECK_RA(lifecycle_has_breadcrumb(evidence_dir, identity.process_id, "ASYNC_BOOTSTRAP_WAIT_DONE"));
    CHECK_RA(fixture.alive());
    delete_file_if_exists_utf8(result.evidence_log_path);
}

void test_synthetic_async_remote_load_stress_100() {
    using namespace deflux::game::gta_sa_de;
    int ok_count = 0;
    for (int i = 0; i < 100; ++i) {
        const std::string evidence_dir = temp_evidence_directory();
        const std::uint64_t session_nonce =
            (0x570000000ull + static_cast<std::uint64_t>(i)) | (steady_ms() & 0xFFFFull);
        FixtureProcess fixture{};
        if (!fixture.start(120000, evidence_dir, true) || !fixture.alive()) {
            std::printf("ASYNC stress fixture start failed at iter %d\n", i);
            break;
        }
        deflux::core::ProcessInstance identity{};
        if (!query_research_target_identity(fixture.pi.dwProcessId, identity)) {
            std::printf("ASYNC stress identity failed at iter %d\n", i);
            break;
        }
        SyntheticActivationE2EOptions options{};
        options.fixture_exe_path = sibling_m1_path(L"DeFlux_ResearchLoader_Fixture.exe");
        options.launcher_module_path = sibling_m1_path(L"DeFlux_Foundation_Tests.exe");
        options.runtime_dll_path = sibling_m1_path(L"DeFlux_Runtime.dll");
        options.evidence_directory_utf8 = evidence_dir;
        options.target = identity;
        options.interval_ms = 50;
        options.max_samples = 3;
        options.session_nonce = session_nonce;
        options.use_async_sampler = true;
        options.wait_timeout_ms = 20000;
        const SyntheticActivationE2EResult result = execute_synthetic_activation_e2e(options);
        const bool pass = result.ok && result.intent_consumed && result.bootstrap_complete
            && result.observation_complete
            && result.final_substate == static_cast<std::uint32_t>(ResearchSubstate::Stopped)
            && result.samples_collected == options.max_samples
            && evidence_log_contains_required_fields(result.evidence_log_path, 3, true)
            && evidence_has_breadcrumb(result.evidence_log_path, "SAMPLER_STOPPED")
            && lifecycle_has_breadcrumb(evidence_dir, identity.process_id, "SAMPLER_THREAD_EXIT")
            && fixture.alive();
        if (!pass) {
            std::printf(
                "ASYNC stress fail iter=%d msg=%s samples=%u substate=%u alive=%d fields=%d stopped=%d exit=%d log=%s\n",
                i,
                result.message.c_str(),
                result.samples_collected,
                result.final_substate,
                fixture.alive() ? 1 : 0,
                evidence_log_contains_required_fields(result.evidence_log_path, 3, true) ? 1 : 0,
                evidence_has_breadcrumb(result.evidence_log_path, "SAMPLER_STOPPED") ? 1 : 0,
                lifecycle_has_breadcrumb(evidence_dir, identity.process_id, "SAMPLER_THREAD_EXIT") ? 1 : 0,
                result.evidence_log_path.c_str());
            break;
        }
        delete_file_if_exists_utf8(result.evidence_log_path);
        ++ok_count;
    }
    CHECK_RA(ok_count == 100);
}

void test_synthetic_async_no_intent_and_wrong_intent() {
    using namespace deflux::game::gta_sa_de;
    {
        FixtureProcess fixture{};
        if (!fixture.start(60000, {}, true)) {
            std::printf("SKIP async no-intent fixture unavailable\n");
            return;
        }
        deflux::core::ProcessInstance identity{};
        CHECK_RA(query_research_target_identity(fixture.pi.dwProcessId, identity));
        SyntheticActivationE2EOptions options{};
        options.fixture_exe_path = sibling_m1_path(L"DeFlux_ResearchLoader_Fixture.exe");
        options.launcher_module_path = sibling_m1_path(L"DeFlux_Foundation_Tests.exe");
        options.runtime_dll_path = sibling_m1_path(L"DeFlux_Runtime.dll");
        options.target = identity;
        options.publish_intent = false;
        options.expect_activation = false;
        options.use_async_sampler = true;
        const SyntheticActivationE2EResult result = execute_synthetic_activation_e2e(options);
        CHECK_RA(result.ok);
        CHECK_RA(result.bootstrap_complete);
        CHECK_RA(result.final_substate == static_cast<std::uint32_t>(ResearchSubstate::Disabled));
        CHECK_RA(fixture.alive());
    }
    {
        FixtureProcess fixture{};
        if (!fixture.start(60000, {}, true)) {
            std::printf("SKIP async wrong-intent fixture unavailable\n");
            return;
        }
        deflux::core::ProcessInstance identity{};
        CHECK_RA(query_research_target_identity(fixture.pi.dwProcessId, identity));
        SyntheticActivationE2EOptions options{};
        options.fixture_exe_path = sibling_m1_path(L"DeFlux_ResearchLoader_Fixture.exe");
        options.launcher_module_path = sibling_m1_path(L"DeFlux_Foundation_Tests.exe");
        options.runtime_dll_path = sibling_m1_path(L"DeFlux_Runtime.dll");
        options.target = identity;
        options.use_intent_override = true;
        options.expect_activation = false;
        options.use_async_sampler = true;
        options.intent_override.target = identity;
        options.intent_override.target.process_id ^= 0xABCDu;
        options.intent_override.synthetic_fixture = true;
        options.intent_override.interval_ms = 50;
        options.intent_override.max_samples = 3;
        const SyntheticActivationE2EResult result = execute_synthetic_activation_e2e(options);
        CHECK_RA(result.ok);
        CHECK_RA(!result.intent_consumed);
        CHECK_RA(result.final_substate == static_cast<std::uint32_t>(ResearchSubstate::Disabled));
        CHECK_RA(fixture.alive());
    }
}

void test_evidence_log_fields_and_no_memory_dump() {
    deflux::game::gta_sa_de::research_evidence_set_output_directory_override(
        temp_evidence_directory());
    deflux::game::gta_sa_de::research_evidence_set_hook_enabled(true);
    deflux::game::gta_sa_de::ResearchEvidenceSessionHeader header{};
    header.process_id = 4242;
    header.creation_identity = 999;
    header.session_nonce = 0xBEEF;
    header.experiment_id = deflux::game::gta_sa_de::kGGameStateInProcessExperimentId;
    header.experiment_version = 1;
    header.expected_sha256 = deflux::game::gta_sa_de::kResearchAllowlistedSha256;
    header.observed_sha256 = deflux::game::gta_sa_de::kResearchAllowlistedSha256;
    std::string log_path;
    CHECK_RA(deflux::game::gta_sa_de::research_evidence_begin_session(header, log_path));
    CHECK_RA(!log_path.empty());
    deflux::game::gta_sa_de::InProcessResearchEvidenceSample sample{};
    sample.sample_index = 0;
    sample.monotonic_time_ms = 1;
    sample.read_ok = true;
    sample.raw_byte = 9;
    sample.substate = deflux::game::gta_sa_de::ResearchSubstate::Observing;
    sample.derivation_method =
        deflux::game::gta_sa_de::GameStateAddressDerivation::MenuMainPresentsLeaCmpImm9;
    sample.derived_global_rva = 0x6000;
    deflux::game::gta_sa_de::research_evidence_append_sample(sample);
    deflux::game::gta_sa_de::research_evidence_end_session(
        deflux::game::gta_sa_de::ResearchSubstate::Stopped,
        deflux::game::gta_sa_de::InProcessSignalClassification::Insufficient,
        "Test complete.");
    wchar_t wide[MAX_PATH]{};
    MultiByteToWideChar(CP_UTF8, 0, log_path.c_str(), -1, wide, MAX_PATH);
    CHECK_RA(GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES);
    deflux::game::gta_sa_de::research_evidence_set_hook_enabled(false);
    deflux::game::gta_sa_de::research_evidence_clear_output_directory_override();
}

void test_runtime_abi_unchanged() {
    CHECK_RA(DEFLUX_RUNTIME_ABI_VERSION == 1u);
    CHECK_RA(sizeof(DefluxRuntimeInitContext) == 12);
}

void test_supported_registry_empty_and_readiness() {
    using deflux::game::gta_sa_de::GtaSaDeAdapter;
    using deflux::game::Readiness;
    const GtaSaDeAdapter adapter;
    CHECK_RA(adapter.engine_ready().status == Readiness::NotImplemented);
    CHECK_RA(adapter.world_ready().status == Readiness::NotImplemented);
    CHECK_RA(adapter.local_player_ready().status == Readiness::NotImplemented);
    CHECK_RA(adapter.probe_supported_game().status != deflux::game::ProbeStatus::Supported);
}

} // namespace

int run_qualification_research_activation_tests(int& passed, int& failed) {
    deflux::game::gta_sa_de::reset_research_intent_mapping_for_tests();
    test_no_intent_record_validation_disabled();
    test_malformed_intent_rejected();
    test_stale_pid_rejected();
    test_stale_creation_identity_rejected();
    test_wrong_sha_rejected();
    test_wrong_experiment_rejected();
    test_wrong_version_rejected();
    test_wrong_capability_rejected();
    test_excessive_bounds_clamped_or_rejected();
    test_valid_synthetic_intent_accepted();
    test_runtime_revalidates_intent_fields();
    test_intent_process_lifetime_scoped();
    test_intent_cleanup_after_publish();
    test_runtime_load_without_intent_no_experiment();
    test_evidence_log_begin_session_no_deadlock_regression();
    test_synthetic_end_to_end_activation_chain();
    test_synthetic_remote_load_without_intent_disabled();
    test_synthetic_wrong_intent_rejected();
    test_synthetic_async_end_to_end_activation_chain();
    test_synthetic_async_remote_load_stress_100();
    test_synthetic_async_no_intent_and_wrong_intent();
    test_evidence_log_fields_and_no_memory_dump();
    test_runtime_abi_unchanged();
    test_supported_registry_empty_and_readiness();
    deflux::game::gta_sa_de::reset_research_intent_mapping_for_tests();
    passed += g_passed;
    failed += g_failed;
    return g_failed == 0 ? 0 : 1;
}

} // namespace deflux_tests
