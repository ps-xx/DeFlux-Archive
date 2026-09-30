#include "deflux/game/gta_sa_de/research_launcher.hpp"

#include "deflux/game/gta_sa_de/adapter.hpp"
#include "deflux/game/gta_sa_de/behavioral_transform_research.hpp"
#include "deflux/game/gta_sa_de/detection.hpp"
#include "deflux/game/gta_sa_de/grounded_live_research.hpp"
#include "deflux/game/gta_sa_de/research_build_allowlist.hpp"
#include "deflux/game/gta_sa_de/research_intent.hpp"
#include "deflux/core/process_lifecycle.hpp"
#include "discovery.hpp"
#include "environment_preflight.hpp"
#include "process_query.hpp"
#include "win_utf8.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdlib>
#include <cstring>

namespace deflux::game::gta_sa_de {
namespace {

bool parse_grounded_live_requested(int argc, char** argv, bool& cleo_absent) {
    cleo_absent = false;
    bool requested = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr) {
            continue;
        }
        if (std::strcmp(argv[i], kGroundedLiveFlag) == 0) {
            requested = true;
        }
        if (std::strcmp(argv[i], kGroundedCleoAbsentFlag) == 0) {
            cleo_absent = true;
        }
    }
    return requested;
}

bool parse_behavioral_requested(int argc, char** argv, BehavioralProtocol& protocol) {
    protocol = BehavioralProtocol::Primary;
    bool requested = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr) {
            continue;
        }
        if (std::strcmp(argv[i], "--behavioral-transform") == 0) {
            requested = true;
        }
        if (std::strcmp(argv[i], "--validation-repeat") == 0) {
            protocol = BehavioralProtocol::Validation;
        }
    }
    return requested;
}

bool parse_inject_requested(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr) {
            continue;
        }
        if (std::strcmp(argv[i], "--inject") == 0) {
            return true;
        }
        if (std::strcmp(argv[i], "--pid") == 0 || std::strcmp(argv[i], "--dll") == 0
            || std::strcmp(argv[i], "--target") == 0) {
            return true;
        }
    }
    return false;
}

bool has_forbidden_override_args(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr) {
            continue;
        }
        if (std::strcmp(argv[i], "--pid") == 0 || std::strcmp(argv[i], "--dll") == 0
            || std::strcmp(argv[i], "--target") == 0) {
            return true;
        }
    }
    return false;
}

std::wstring launcher_module_path() {
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
        return {};
    }
    return detail::canonicalize_file_path(path);
}

bool build_preflight(ResearchInjectPreflight& out, std::string& message) {
    const GtaSaDeAdapter adapter;
    const DetectionSnapshot detection = adapter.detect();
    if (detection.installation != InstallationStatus::Found || detection.executable_path.empty()) {
        message = "GTA SA DE installation not found for research preflight.";
        return false;
    }
    out.executable_path = detection.executable_path;
    out.sha256_hex = detection.sha256_hex;
    if (!is_research_allowlisted_sha256(out.sha256_hex)) {
        message = "Executable SHA-256 is not on the research allowlist.";
        return false;
    }
    if (is_production_supported_sha256(out.sha256_hex)) {
        message = "Research allowlist must not overlap production supported registry.";
        return false;
    }
    const EnvironmentPreflightResult preflight =
        detail::scan_game_executable_directory(out.executable_path);
    if (!preflight.clean) {
        message = preflight.message;
        return false;
    }
    out.environment_clean = true;
    return true;
}

std::uint32_t parse_coverage_window(int argc, char** argv) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (argv[index] != nullptr && std::strcmp(argv[index], "--coverage-window") == 0
            && argv[index + 1] != nullptr) {
            return static_cast<std::uint32_t>(std::strtoul(argv[index + 1], nullptr, 10));
        }
    }
    return 0;
}

bool live_target_isolation_engaged() noexcept {
    char value[8]{};
    const DWORD length = GetEnvironmentVariableA("DEFLUX_RESEARCH_ISOLATE_LIVE_TARGET", value, static_cast<DWORD>(sizeof(value)));
    return length == 1 && value[0] == '1';
}

bool resolve_verified_gta_target(const std::wstring& expected_exe,
                                 deflux::core::ProcessInstance& target,
                                 std::string& message) {
    const detail::VerifiedProcess verified = detail::query_verified_process(expected_exe);
    if (verified.status != detail::VerifiedQueryStatus::Found) {
        message = verified.message.empty() ? "Research target is not running." : verified.message;
        return false;
    }
    target.process_id = verified.process_id;
    target.creation_identity = verified.creation_identity;
    target.executable_path = detail::wide_to_utf8(verified.image_path);
    if (!deflux::core::is_valid_process_instance(target)) {
        message = "Verified GTA process identity is incomplete.";
        return false;
    }
    return true;
}

} // namespace

ResearchLauncherResult run_research_launcher(int argc, char** argv) noexcept {
    ResearchLauncherResult result{};
    bool cleo_absent = false;
    if (parse_grounded_live_requested(argc, argv, cleo_absent)) {
        result.inject_requested = false;
        if (has_forbidden_override_args(argc, argv)) {
            result.ok = false;
            result.load_result = ResearchLoadResult::ArbitraryTargetRejected;
            result.message =
                "Arbitrary --pid/--dll/--target overrides are rejected in research mode.";
            return result;
        }
        if (live_target_isolation_engaged()) {
            result.ok = false;
            result.load_result = ResearchLoadResult::TargetNotRunning;
            result.message = "Live GTA resolution is isolated from automated tests.";
            return result;
        }
        const int console = run_grounded_live_console(cleo_absent, result.message);
        result.ok = console == 0;
        result.load_result = console == 0 ? ResearchLoadResult::LoadNotRequested
                                          : ResearchLoadResult::TargetNotRunning;
        return result;
    }

    BehavioralProtocol behavioral_protocol = BehavioralProtocol::Primary;
    const bool behavioral = parse_behavioral_requested(argc, argv, behavioral_protocol);
    result.inject_requested = parse_inject_requested(argc, argv) || behavioral;

    ResearchInjectPreflight preflight{};
    if (!build_preflight(preflight, result.message)) {
        result.load_result = ResearchLoadResult::PreflightFailed;
        return result;
    }
    result.executable_path = preflight.executable_path;
    result.sha256_hex = preflight.sha256_hex;
    result.ok = true;
    result.message = "Research launcher preflight passed.";

    if (!result.inject_requested) {
        result.message = "Research launcher preflight passed (no injection requested).";
        return result;
    }

    if (has_forbidden_override_args(argc, argv)) {
        result.ok = false;
        result.load_result = ResearchLoadResult::ArbitraryTargetRejected;
        result.message =
            "Arbitrary --pid/--dll/--target overrides are rejected in research mode.";
        return result;
    }

    if (live_target_isolation_engaged()) {
        result.ok = false;
        result.load_result = ResearchLoadResult::TargetNotRunning;
        result.message = "Live GTA resolution is isolated from automated tests.";
        return result;
    }

    const std::wstring expected_exe = detail::utf8_to_wide(preflight.executable_path);
    deflux::core::ProcessInstance observed{};
    if (!resolve_verified_gta_target(expected_exe, observed, result.message)) {
        result.ok = false;
        result.load_result = ResearchLoadResult::TargetNotRunning;
        return result;
    }

    const std::wstring launcher_path = launcher_module_path();
    if (launcher_path.empty()) {
        result.ok = false;
        result.load_result = ResearchLoadResult::RuntimeDllNotFound;
        result.message = "Could not resolve research launcher module path.";
        return result;
    }

    const auto gta_dir = [&]() -> std::wstring {
        const auto slash = expected_exe.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring{} : expected_exe.substr(0, slash);
    }();

    ResearchRemoteLoadRequest request{};
    request.target_mode = ResearchLoadTargetMode::GtaSaDeResearchOnly;
    request.load_requested = true;
    request.preflight = preflight;
    request.observed_target = observed;
    request.pre_load_target = observed;

    std::string runtime_error;
    if (!resolve_trusted_runtime_dll_path(
            request.runtime_dll_path, launcher_path, gta_dir, runtime_error)) {
        result.ok = false;
        result.load_result = ResearchLoadResult::RuntimeDllNotFound;
        result.message = runtime_error;
        return result;
    }

    ResearchIntentSession intent_session{};
    ResearchIntentPublishRequest intent_request{};
    intent_request.target = observed;
    intent_request.expected_sha256 = preflight.sha256_hex.c_str();
    intent_request.synthetic_fixture = false;
    if (behavioral) {
        intent_request.experiment_id = kBehavioralTransformExperimentId;
    }
    std::string intent_message;
    if (!publish_research_intent_session(intent_session, intent_request, intent_message)) {
        result.ok = false;
        result.load_result = ResearchLoadResult::PreflightFailed;
        result.message = intent_message;
        return result;
    }

    result.load_diagnostics = execute_research_remote_load(request);
    result.load_result = result.load_diagnostics.result;
    result.ok = result.load_result == ResearchLoadResult::LoadSucceeded;
    result.message = result.load_diagnostics.message;

    if (result.load_result == ResearchLoadResult::LoadSucceeded) {
        (void)wait_for_research_intent_consumed(5000, intent_message);
    } else {
        (void)mark_research_intent_rejected(intent_message);
    }
    const std::uint64_t session_nonce = intent_session.view != nullptr
        ? intent_session.view->session_nonce
        : 0;
    close_research_intent_session(intent_session);
    if (behavioral && result.ok && session_nonce != 0) {
        const int console = run_behavioral_phase_console(
            session_nonce, behavioral_protocol, parse_coverage_window(argc, argv));
        if (console != 0) {
            result.ok = false;
            result.message = "Behavioral phase console stopped before every marker was captured.";
        }
    }
    return result;
}

} // namespace deflux::game::gta_sa_de
