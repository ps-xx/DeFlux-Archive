#include "deflux/game/gta_sa_de/gworld_live_observation.hpp"

#include "deflux/game/gta_sa_de/qualification.hpp"
#include "environment_preflight.hpp"
#include "fingerprint.hpp"
#include "module_query.hpp"
#include "observation.hpp"
#include "remote_read.hpp"
#include "win_utf8.hpp"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <thread>
#include <unordered_set>

namespace deflux::game::gta_sa_de {
namespace {

std::string lower_copy(std::string_view text) {
    std::string out(text);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

deflux::core::ProcessInstance process_instance_from_query(std::uint32_t pid,
                                                            std::uint64_t creation,
                                                            const std::wstring& path) {
    deflux::core::ProcessInstance instance{};
    instance.process_id = pid;
    instance.creation_identity = creation;
    instance.executable_path = detail::wide_to_utf8(path);
    return instance;
}

bool verify_process_identity(const deflux::core::ProcessInstance& expected,
                             detail::ScopedProcess& process,
                             std::string& message_out) {
    std::uint32_t live_pid = 0;
    std::uint64_t live_created = 0;
    std::wstring live_path;
    if (!process.query_identity(live_pid, live_created, live_path)) {
        message_out = "Could not re-query process identity.";
        return false;
    }

    const deflux::core::ProcessInstance live =
        process_instance_from_query(live_pid, live_created, live_path);
    if (!process_identity_matches(expected, live)) {
        message_out = "Process identity changed during observation; aborting.";
        return false;
    }
    return true;
}

LiveObservationSessionResult make_status(LiveObservationStatus status, std::string message) {
    LiveObservationSessionResult result{};
    result.status = status;
    result.message = std::move(message);
    return result;
}

std::uint64_t steady_timestamp_ms() {
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
}

void sleep_interval_ms(std::uint32_t interval_ms) {
    if (interval_ms == 0) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
}

} // namespace

bool is_architect_approved_candidate_rva(std::uint32_t candidate_rva) noexcept {
    for (const std::uint32_t approved : kArchitectApprovedCandidateRvas) {
        if (approved == candidate_rva) {
            return true;
        }
    }
    return false;
}

bool sha256_matches_live_observation_target(std::string_view sha256_hex) noexcept {
    if (sha256_hex.size() != 64) {
        return false;
    }
    return lower_copy(sha256_hex) == kLiveObservationTargetSha256;
}

bool process_identity_matches(const deflux::core::ProcessInstance& expected,
                              const deflux::core::ProcessInstance& observed) noexcept {
    return deflux::core::same_process_instance(expected, observed)
        && executable_paths_match(expected.executable_path, observed.executable_path);
}

LiveObservationPhase parse_live_observation_phase(std::string_view label) noexcept {
    std::string normalized;
    normalized.reserve(label.size());
    for (char ch : label) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            continue;
        }
        normalized.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }

    if (normalized == "MENU") {
        return LiveObservationPhase::Menu;
    }
    if (normalized == "LOADING") {
        return LiveObservationPhase::Loading;
    }
    if (normalized == "GAMEPLAY_STANDING") {
        return LiveObservationPhase::GameplayStanding;
    }
    if (normalized == "GAMEPLAY_MOVING") {
        return LiveObservationPhase::GameplayMoving;
    }
    if (normalized == "PAUSE_MENU") {
        return LiveObservationPhase::PauseMenu;
    }
    return LiveObservationPhase::Unknown;
}

const char* to_string(LiveObservationPhase phase) noexcept {
    switch (phase) {
    case LiveObservationPhase::Menu:
        return "MENU";
    case LiveObservationPhase::Loading:
        return "LOADING";
    case LiveObservationPhase::GameplayStanding:
        return "GAMEPLAY_STANDING";
    case LiveObservationPhase::GameplayMoving:
        return "GAMEPLAY_MOVING";
    case LiveObservationPhase::PauseMenu:
        return "PAUSE_MENU";
    case LiveObservationPhase::Unknown:
        return "UNKNOWN";
    }
    return "UNKNOWN";
}

const char* to_string(LiveObservationStatus status) noexcept {
    switch (status) {
    case LiveObservationStatus::Ok:
        return "OK";
    case LiveObservationStatus::NotRunning:
        return "NOT_RUNNING";
    case LiveObservationStatus::BuildMismatch:
        return "BUILD_MISMATCH";
    case LiveObservationStatus::ProcessChanged:
        return "PROCESS_CHANGED";
    case LiveObservationStatus::ModuleInvalid:
        return "MODULE_INVALID";
    case LiveObservationStatus::EnvironmentContaminated:
        return "ENVIRONMENT_CONTAMINATED";
    case LiveObservationStatus::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

const char* to_string(LiveObservationConsistency consistency) noexcept {
    switch (consistency) {
    case LiveObservationConsistency::Consistent:
        return "OBSERVATION_CONSISTENT";
    case LiveObservationConsistency::Inconsistent:
        return "OBSERVATION_INCONSISTENT";
    case LiveObservationConsistency::InsufficientData:
        return "INSUFFICIENT_DATA";
    }
    return "UNKNOWN";
}

bool address_in_loaded_module(std::uint64_t address,
                               const std::vector<LiveObservationModuleRange>& ranges,
                               std::uint64_t& module_base_out) noexcept {
    module_base_out = 0;
    if (address == 0) {
        return false;
    }
    for (const LiveObservationModuleRange& range : ranges) {
        if (pointer_in_range(address, range.base, range.size)) {
            module_base_out = range.base;
            return true;
        }
    }
    return false;
}

LiveObservationSample probe_candidate_slot(std::uint32_t candidate_rva,
                                             const LiveObservationProbeContext& context,
                                             const LiveObservationReadFn& read,
                                             LiveObservationPhase phase,
                                             std::uint32_t sample_index,
                                             std::uint64_t timestamp_ms) noexcept {
    LiveObservationSample sample{};
    sample.sample_index = sample_index;
    sample.timestamp_ms = timestamp_ms;
    sample.phase = phase;
    sample.candidate_rva = candidate_rva;
    sample.module_base = context.module_base;

    if (!is_architect_approved_candidate_rva(candidate_rva) || context.module_base == 0
        || context.module_size < 8 || !read) {
        return sample;
    }

    const std::uint64_t slot_va = context.module_base + static_cast<std::uint64_t>(candidate_rva);
    if (slot_va < context.module_base
        || slot_va + 8 > context.module_base + context.module_size) {
        return sample;
    }

    sample.slot_virtual_address = slot_va;
    std::uint64_t slot_value = 0;
    sample.slot_read_ok = read(slot_va, &slot_value, sizeof(slot_value));
    sample.slot_value = slot_value;
    sample.slot_is_null = sample.slot_read_ok && slot_value == 0;
    sample.slot_is_canonical = sample.slot_read_ok && is_canonical_user_pointer(slot_value);

    if (!sample.slot_read_ok || slot_value == 0) {
        return sample;
    }

    sample.object_probe_read_ok = read(slot_value, &sample.first_pointer_value, sizeof(sample.first_pointer_value));
    sample.first_value_readable = sample.object_probe_read_ok;
    if (sample.first_value_readable) {
        sample.first_value_in_module =
            address_in_loaded_module(sample.first_pointer_value, context.module_ranges, sample.first_value_module_base);
    }

    return sample;
}

LiveObservationConsistency classify_live_observation_consistency(
    std::size_t samples,
    std::size_t read_successes,
    std::size_t distinct_pointer_values) noexcept {
    if (samples == 0 || read_successes == 0) {
        return LiveObservationConsistency::InsufficientData;
    }
    if (distinct_pointer_values <= 1) {
        return LiveObservationConsistency::Consistent;
    }
    return LiveObservationConsistency::Inconsistent;
}

LiveObservationPhaseSummary summarize_live_observation_phase(
    std::uint32_t candidate_rva,
    LiveObservationPhase phase,
    const std::vector<LiveObservationSample>& samples) noexcept {
    LiveObservationPhaseSummary summary{};
    summary.phase = phase;

    std::unordered_set<std::uint64_t> distinct_values;
    for (const LiveObservationSample& sample : samples) {
        if (sample.candidate_rva != candidate_rva || sample.phase != phase) {
            continue;
        }
        ++summary.samples;
        if (sample.slot_read_ok) {
            ++summary.read_successes;
            if (sample.slot_is_null) {
                ++summary.null_samples;
            } else {
                ++summary.non_null_samples;
                distinct_values.insert(sample.slot_value);
            }
            if (sample.first_value_in_module) {
                ++summary.first_value_in_module_samples;
            }
        }
    }

    summary.distinct_pointer_values = distinct_values.size();
    summary.pointer_stable = summary.read_successes > 0 && summary.distinct_pointer_values <= 1;
    summary.consistency = classify_live_observation_consistency(
        summary.samples, summary.read_successes, summary.distinct_pointer_values);
    return summary;
}

LiveObservationCandidateSummary summarize_live_observation_candidate(
    std::uint32_t candidate_rva,
    const std::vector<LiveObservationSample>& samples) noexcept {
    LiveObservationCandidateSummary summary{};
    summary.candidate_rva = candidate_rva;

    std::unordered_set<std::uint64_t> distinct_values;
    std::array<bool, 6> phase_seen{};
    for (const LiveObservationSample& sample : samples) {
        if (sample.candidate_rva != candidate_rva) {
            continue;
        }
        ++summary.samples;
        if (sample.slot_read_ok) {
            ++summary.read_successes;
            if (sample.slot_is_null) {
                ++summary.null_samples;
            } else {
                ++summary.non_null_samples;
                distinct_values.insert(sample.slot_value);
            }
            if (sample.first_value_in_module) {
                ++summary.first_value_in_module_samples;
            }
        }
        const std::size_t phase_index = static_cast<std::size_t>(sample.phase);
        if (phase_index < phase_seen.size()) {
            phase_seen[phase_index] = true;
        }
    }

    summary.distinct_pointer_values = distinct_values.size();
    summary.pointer_stable = summary.read_successes > 0 && summary.distinct_pointer_values <= 1;
    summary.consistency = classify_live_observation_consistency(
        summary.samples, summary.read_successes, summary.distinct_pointer_values);

    static constexpr LiveObservationPhase kPhases[] = {
        LiveObservationPhase::Menu,
        LiveObservationPhase::Loading,
        LiveObservationPhase::GameplayStanding,
        LiveObservationPhase::GameplayMoving,
        LiveObservationPhase::PauseMenu,
    };
    for (LiveObservationPhase phase : kPhases) {
        const std::size_t phase_index = static_cast<std::size_t>(phase);
        if (phase_index >= phase_seen.size() || !phase_seen[phase_index]) {
            continue;
        }
        summary.by_phase.push_back(summarize_live_observation_phase(candidate_rva, phase, samples));
    }

    return summary;
}

bool normalize_live_observation_config(LiveObservationConfig& config) noexcept {
    if (config.expected_sha256 == nullptr || config.expected_sha256[0] == '\0') {
        config.expected_sha256 = kLiveObservationTargetSha256;
    }
    if (config.interval_ms == 0) {
        config.interval_ms = kLiveObservationDefaultIntervalMs;
    }
    if (config.max_samples == 0) {
        config.max_samples = kLiveObservationDefaultMaxSamples;
    }
    if (config.interval_ms > kLiveObservationMaxIntervalMs) {
        config.interval_ms = kLiveObservationMaxIntervalMs;
    }
    if (config.max_samples > kLiveObservationMaxSamples) {
        config.max_samples = kLiveObservationMaxSamples;
    }
    return sha256_matches_live_observation_target(config.expected_sha256);
}

LiveObservationSessionResult run_live_candidate_observation_session(
    const deflux::core::ProcessObservation& observation,
    LiveObservationConfig config,
    const std::function<bool(LiveObservationPhase& phase_out)>& next_phase) {
    if (!normalize_live_observation_config(config)) {
        return make_status(LiveObservationStatus::BuildMismatch,
                           "Live observation requires the architect-approved target SHA-256.");
    }
    if (!next_phase) {
        return make_status(LiveObservationStatus::Error, "Phase provider callback is required.");
    }

    if (observation.state == deflux::core::ProcessLifecycle::ObservationError) {
        return make_status(LiveObservationStatus::Error,
                           observation.message.empty() ? std::string("Process observation error.")
                                                       : observation.message);
    }
    if (observation.state != deflux::core::ProcessLifecycle::ProcessDetected
        || !deflux::core::is_valid_process_instance(observation.instance)) {
        return make_status(LiveObservationStatus::NotRunning, "No verified GTA process is running.");
    }

    const EnvironmentPreflightResult preflight =
        scan_game_executable_directory(observation.instance.executable_path);
    if (!preflight.clean) {
        LiveObservationSessionResult result =
            make_status(LiveObservationStatus::EnvironmentContaminated, preflight.message);
        result.process_id = observation.instance.process_id;
        return result;
    }

    const std::wstring path = detail::utf8_to_wide(observation.instance.executable_path);
    const auto fingerprint = detail::fingerprint_executable(path);
    if (!fingerprint.ok || !sha256_matches_live_observation_target(fingerprint.sha256_hex)) {
        return make_status(LiveObservationStatus::BuildMismatch,
                           "Executable SHA-256 does not match the live observation target build.");
    }

    detail::ScopedProcess process;
    if (!process.open_read_only(observation.instance.process_id)) {
        return make_status(LiveObservationStatus::Error, "OpenProcess(QUERY_LIMITED|VM_READ) failed.");
    }

    std::string identity_message;
    if (!verify_process_identity(observation.instance, process, identity_message)) {
        LiveObservationSessionResult result =
            make_status(LiveObservationStatus::ProcessChanged, identity_message);
        result.process_id = observation.instance.process_id;
        return result;
    }

    const auto module = detail::query_main_module(observation.instance.process_id, path);
    if (!module.found || module.base == 0 || module.size == 0) {
        LiveObservationSessionResult result =
            make_status(LiveObservationStatus::ModuleInvalid,
                        module.message.empty() ? std::string("Main module discovery failed.")
                                               : module.message);
        result.process_id = observation.instance.process_id;
        return result;
    }

    std::vector<std::uint8_t> headers(0x400, 0);
    if (!process.read_exact(module.base, headers.data(), headers.size())) {
        return make_status(LiveObservationStatus::ModuleInvalid,
                           "Failed to read PE headers from the main module.");
    }

    PeImageInfo pe{};
    PeParseStatus pe_status = parse_pe64_headers(headers.data(), headers.size(), pe);
    if (pe_status == PeParseStatus::Ok && pe.size_of_headers > headers.size() && pe.size_of_headers <= 0x2000u) {
        headers.assign(pe.size_of_headers, 0);
        if (!process.read_exact(module.base, headers.data(), headers.size())) {
            return make_status(LiveObservationStatus::ModuleInvalid,
                               "Failed to read full PE headers from the main module.");
        }
        pe_status = parse_pe64_headers(headers.data(), headers.size(), pe);
    }
    if (pe_status != PeParseStatus::Ok || pe.size_of_image != static_cast<std::uint32_t>(module.size)) {
        return make_status(LiveObservationStatus::ModuleInvalid, "PE validation failed for live observation.");
    }

    for (const std::uint32_t candidate_rva : kArchitectApprovedCandidateRvas) {
        if (candidate_rva >= pe.size_of_image || static_cast<std::uint64_t>(candidate_rva) + 8ull > module.size) {
            return make_status(LiveObservationStatus::ModuleInvalid,
                               "Architect-approved candidate RVA is outside the validated image.");
        }
        if (!pe_rva_in_non_executable_section(headers.data(), headers.size(), candidate_rva)) {
            return make_status(LiveObservationStatus::ModuleInvalid,
                               "Architect-approved candidate RVA is not in a non-executable section.");
        }
    }

    std::vector<detail::ModuleRange> module_ranges;
    if (!detail::query_loaded_module_ranges(observation.instance.process_id, module_ranges)) {
        return make_status(LiveObservationStatus::ModuleInvalid,
                           "Failed to enumerate loaded module ranges.");
    }

    LiveObservationProbeContext probe_context{};
    probe_context.module_base = module.base;
    probe_context.module_size = module.size;
    probe_context.module_ranges.reserve(module_ranges.size());
    for (const detail::ModuleRange& range : module_ranges) {
        probe_context.module_ranges.push_back(LiveObservationModuleRange{range.base, range.size});
    }

    const LiveObservationReadFn read_fn = [&process](std::uint64_t address, void* out, std::size_t bytes) {
        return process.read_exact(address, out, bytes);
    };

    LiveObservationSessionResult result{};
    result.status = LiveObservationStatus::Ok;
    result.process_id = observation.instance.process_id;
    result.module_base = module.base;
    result.module_size = module.size;
    result.message = "Live candidate observation session completed.";

    for (std::uint32_t sample_index = 0; sample_index < config.max_samples; ++sample_index) {
        LiveObservationPhase phase = LiveObservationPhase::Unknown;
        if (!next_phase(phase)) {
            break;
        }
        if (phase == LiveObservationPhase::Unknown) {
            result.message = "Unknown phase label; stopping observation session.";
            break;
        }

        if (!verify_process_identity(observation.instance, process, identity_message)) {
            result.status = LiveObservationStatus::ProcessChanged;
            result.message = identity_message;
            break;
        }

        const std::uint64_t timestamp_ms = steady_timestamp_ms();
        for (const std::uint32_t candidate_rva : kArchitectApprovedCandidateRvas) {
            result.samples.push_back(probe_candidate_slot(
                candidate_rva, probe_context, read_fn, phase, sample_index, timestamp_ms));
        }

        if (!verify_process_identity(observation.instance, process, identity_message)) {
            result.status = LiveObservationStatus::ProcessChanged;
            result.message = identity_message;
            break;
        }

        ++result.samples_collected;
        if (sample_index + 1 >= config.max_samples) {
            break;
        }
        sleep_interval_ms(config.interval_ms);
    }

    for (const std::uint32_t candidate_rva : kArchitectApprovedCandidateRvas) {
        result.summaries.push_back(summarize_live_observation_candidate(candidate_rva, result.samples));
    }

    if (result.status == LiveObservationStatus::Ok && result.samples.empty()) {
        result.message = "Observation session stopped before any labeled samples were collected.";
    }

    process.close();
    return result;
}

} // namespace deflux::game::gta_sa_de
