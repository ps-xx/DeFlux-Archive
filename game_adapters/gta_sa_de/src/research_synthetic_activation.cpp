#include "deflux/game/gta_sa_de/research_synthetic_activation.hpp"

#include "deflux/game/gta_sa_de/in_process_research.hpp"
#include "deflux/game/gta_sa_de/research_activation_status.hpp"
#include "win_utf8.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace deflux::game::gta_sa_de {
namespace {

SyntheticActivationE2EResult make_result(bool ok, std::string message) {
    SyntheticActivationE2EResult result{};
    result.ok = ok;
    result.message = std::move(message);
    return result;
}

void read_activation_status(SyntheticActivationE2EResult& result, std::uint64_t session_nonce) {
    ResearchActivationStatusRecord status{};
    bool found = false;
    if (research_activation_status_try_read(status, found, session_nonce) && found) {
        result.bootstrap_complete = status.bootstrap_complete != 0;
        result.final_substate = status.research_substate;
        result.samples_collected = status.samples_collected;
        result.observation_complete = status.observation_complete != 0;
        result.pipeline_proof = status.pipeline_proof != 0;
    }
}

} // namespace

SyntheticActivationE2EResult execute_synthetic_activation_e2e(
    const SyntheticActivationE2EOptions& options) noexcept {
    SyntheticActivationE2EResult result{};
    if (options.fixture_exe_path.empty() || options.runtime_dll_path.empty()
        || options.launcher_module_path.empty() || options.target.process_id == 0) {
        result.message = "Synthetic E2E options are incomplete.";
        return result;
    }

    research_activation_status_reset_for_tests(options.session_nonce);
    reset_research_intent_mapping_for_tests();

    ResearchIntentSession intent_session{};
    if (options.publish_intent) {
        ResearchIntentPublishRequest request =
            options.use_intent_override ? options.intent_override : ResearchIntentPublishRequest{};
        if (!options.use_intent_override) {
            request.target = options.target;
            request.synthetic_fixture = true;
            request.interval_ms = options.interval_ms;
            request.max_samples = options.max_samples;
            request.session_nonce = options.session_nonce;
        }
        if (!publish_research_intent_session(intent_session, request, result.message)) {
            return result;
        }
    }

    const auto load_diag = execute_research_remote_load_synthetic_fixture(
        options.fixture_exe_path,
        options.launcher_module_path,
        options.runtime_dll_path,
        options.target.process_id,
        options.target);
    result.load_result = load_diag.result;
    if (load_diag.result != ResearchLoadResult::LoadSucceeded) {
        result.message = load_diag.message.empty() ? "Synthetic remote load failed." : load_diag.message;
        close_research_intent_session(intent_session);
        return result;
    }

    if (options.publish_intent) {
        if (options.expect_activation) {
            std::string wait_message;
            result.intent_consumed =
                wait_for_research_intent_consumed(options.wait_timeout_ms, wait_message);
            if (!result.message.empty()) {
                result.message += " ";
            }
            result.message += wait_message;
        } else {
            ResearchIntentRecord record{};
            bool found = false;
            std::string open_message;
            for (std::uint32_t elapsed = 0; elapsed < 2000; elapsed += 50) {
                if (!try_open_research_intent_record(record, found, open_message)) {
                    result.message = open_message;
                    close_research_intent_session(intent_session);
                    return result;
                }
                if (found
                    && record.state
                           == static_cast<std::uint32_t>(ResearchIntentState::Rejected)) {
                    break;
                }
                Sleep(50);
            }
            result.intent_consumed =
                found && record.state == static_cast<std::uint32_t>(ResearchIntentState::Consumed);
        }
    }

    ResearchActivationStatusRecord status{};
    if (options.expect_activation) {
        if (!research_activation_status_wait_for(
                static_cast<std::uint32_t>(ResearchSubstate::Stopped),
                1,
                true,
                options.wait_timeout_ms,
                options.session_nonce,
                status)) {
            read_activation_status(result, options.session_nonce);
            if (!options.evidence_directory_utf8.empty()) {
                result.evidence_log_path =
                    find_latest_evidence_log_in_directory(options.evidence_directory_utf8,
                                                          options.session_nonce);
            }
            result.message = "Timed out waiting for synthetic observation STOPPED.";
            close_research_intent_session(intent_session);
            return result;
        }
    } else {
        const auto deadline = GetTickCount64() + options.wait_timeout_ms;
        bool ready = false;
        while (GetTickCount64() < deadline) {
            bool found = false;
            if (!research_activation_status_try_read(status, found, 0) || !found) {
                Sleep(10);
                continue;
            }
            if (status.bootstrap_complete != 0
                && status.research_substate
                       == static_cast<std::uint32_t>(ResearchSubstate::Disabled)) {
                ready = true;
                break;
            }
            Sleep(10);
        }
        if (!ready) {
            result.message = "Timed out waiting for bootstrap without activation.";
            close_research_intent_session(intent_session);
            return result;
        }
    }

    result.bootstrap_complete = status.bootstrap_complete != 0;
    result.final_substate = status.research_substate;
    result.samples_collected = status.samples_collected;
    result.observation_complete = status.observation_complete != 0;
    result.pipeline_proof = status.pipeline_proof != 0;

    if (!options.evidence_directory_utf8.empty() && options.expect_activation) {
        result.evidence_log_path =
            find_latest_evidence_log_in_directory(options.evidence_directory_utf8,
                                                  options.session_nonce);
    }

    close_research_intent_session(intent_session);

    const bool intent_ok = !options.publish_intent
        || (options.expect_activation ? result.intent_consumed : !result.intent_consumed);
    const bool status_ok =
        options.expect_activation
            ? (result.final_substate == static_cast<std::uint32_t>(ResearchSubstate::Stopped)
               && result.samples_collected == options.max_samples
               && result.observation_complete)
            : (result.final_substate == static_cast<std::uint32_t>(ResearchSubstate::Disabled));

    result.ok = intent_ok && status_ok && result.bootstrap_complete;
    if (result.ok) {
        result.message = "Synthetic activation E2E succeeded.";
    }
    return result;
}

std::string find_latest_evidence_log_in_directory(const std::string& directory_utf8,
                                                  std::uint64_t session_nonce) noexcept {
    char expected_suffix[64]{};
    std::snprintf(expected_suffix,
                  sizeof(expected_suffix),
                  "_%llu.log",
                  static_cast<unsigned long long>(session_nonce));
    const std::wstring pattern = detail::utf8_to_wide(directory_utf8 + "*ggamestate_observation_*");
    WIN32_FIND_DATAW entry{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &entry);
    if (find == INVALID_HANDLE_VALUE) {
        return {};
    }
    std::string best_path;
    FILETIME best_write{};
    bool have_best = false;
    do {
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        const std::string name = detail::wide_to_utf8(entry.cFileName);
        if (name.find(expected_suffix) == std::string::npos) {
            continue;
        }
        const std::string path = directory_utf8 + name;
        if (!have_best
            || CompareFileTime(&entry.ftLastWriteTime, &best_write) > 0) {
            best_write = entry.ftLastWriteTime;
            best_path = path;
            have_best = true;
        }
    } while (FindNextFileW(find, &entry));
    FindClose(find);
    return best_path;
}

bool evidence_log_contains_required_fields(const std::string& log_path,
                                           std::uint32_t max_samples,
                                           bool expect_pipeline_proof) noexcept {
    std::ifstream input(log_path);
    if (!input.is_open()) {
        return false;
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    const std::string content = buffer.str();
    if (content.find("SESSION_START") == std::string::npos
        || content.find("SAMPLE index=") == std::string::npos
        || content.find("SESSION_END") == std::string::npos) {
        return false;
    }
    if (content.find("MEMORY_DUMP") != std::string::npos
        || content.find("HEXDUMP") != std::string::npos) {
        return false;
    }
    if (expect_pipeline_proof && content.find("pipeline_proof=1") == std::string::npos) {
        return false;
    }
    std::size_t sample_count = 0;
    std::size_t pos = 0;
    while ((pos = content.find("SAMPLE index=", pos)) != std::string::npos) {
        ++sample_count;
        ++pos;
    }
    if (sample_count != max_samples) {
        return false;
    }
    for (std::uint32_t index = 0; index < max_samples; ++index) {
        char token[48]{};
        std::snprintf(token, sizeof(token), "SAMPLE index=%u time_ms=", index);
        if (content.find(token) == std::string::npos) {
            return false;
        }
    }
    return true;
}

void delete_file_if_exists_utf8(const std::string& path_utf8) noexcept {
    if (path_utf8.empty()) {
        return;
    }
    DeleteFileW(detail::utf8_to_wide(path_utf8).c_str());
}

} // namespace deflux::game::gta_sa_de
