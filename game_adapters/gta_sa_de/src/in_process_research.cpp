#include "deflux/game/gta_sa_de/behavioral_transform_research.hpp"
#include "deflux/game/gta_sa_de/in_process_research.hpp"

#include "deflux/game/gta_sa_de/passive_same_process_read.hpp"
#include "deflux/game/gta_sa_de/research_build_allowlist.hpp"
#include "deflux/game/gta_sa_de/research_activation_status.hpp"
#include "deflux/game/gta_sa_de/research_evidence_log.hpp"
#include "deflux/game/gta_sa_de/research_intent.hpp"
#include "environment_preflight.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <process.h>

#include <chrono>
#include <cstring>
#include <exception>

namespace deflux::game::gta_sa_de {
namespace {

std::uint64_t steady_ms() {
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch())
            .count());
}

void sleep_ms(std::uint32_t ms) {
    if (ms == 0) {
        return;
    }
    Sleep(ms);
}

unsigned __stdcall sampler_thread_trampoline(void* context) {
    auto* controller = static_cast<InProcessResearchController*>(context);
    if (controller == nullptr) {
        research_evidence_append_lifecycle("SAMPLER_THREAD_ENTER", "null_controller");
        return 0;
    }
    research_evidence_append_lifecycle("SAMPLER_THREAD_ENTER", "");
    try {
        controller->execute_sampler_loop();
    } catch (const std::exception& ex) {
        research_evidence_append_lifecycle("SAMPLER_EXCEPTION", ex.what());
        std::string message;
        (void)controller->shutdown(message);
    } catch (...) {
        research_evidence_append_lifecycle("SAMPLER_EXCEPTION", "unknown");
        std::string message;
        (void)controller->shutdown(message);
    }
    research_evidence_append_lifecycle("SAMPLER_THREAD_EXIT", "");
    return 0;
}

} // namespace

const char* to_string(ResearchCapability capability) noexcept {
    switch (capability) {
    case ResearchCapability::None:
        return "NONE";
    case ResearchCapability::PassiveRead:
        return "PASSIVE_READ";
    case ResearchCapability::Write:
        return "WRITE";
    case ResearchCapability::Hook:
        return "HOOK";
    }
    return "UNKNOWN";
}

const char* to_string(ResearchSubstate substate) noexcept {
    switch (substate) {
    case ResearchSubstate::Disabled:
        return "DISABLED";
    case ResearchSubstate::Armed:
        return "ARMED";
    case ResearchSubstate::Observing:
        return "OBSERVING";
    case ResearchSubstate::Stopping:
        return "STOPPING";
    case ResearchSubstate::Stopped:
        return "STOPPED";
    case ResearchSubstate::Failed:
        return "FAILED";
    }
    return "UNKNOWN";
}

const char* to_string(InProcessSignalClassification classification) noexcept {
    switch (classification) {
    case InProcessSignalClassification::None:
        return "NONE";
    case InProcessSignalClassification::Insufficient:
        return "INSUFFICIENT";
    case InProcessSignalClassification::Reproduced:
        return "IN_PROCESS_SIGNAL_REPRODUCED";
    case InProcessSignalClassification::Contradicted:
        return "CONTRADICTED";
    }
    return "UNKNOWN";
}

bool is_supported_in_process_experiment_id(const char* experiment_id) noexcept {
    return experiment_id != nullptr
        && (std::strcmp(experiment_id, kGGameStateInProcessExperimentId) == 0
            || std::strcmp(experiment_id, kBehavioralTransformExperimentId) == 0);
}

bool validate_research_capability(ResearchCapability capability,
                                  std::string& rejection_reason) noexcept {
    if (capability == ResearchCapability::PassiveRead) {
        return true;
    }
    rejection_reason = "Only PASSIVE_READ capability is supported in Task #9B.";
    return false;
}

bool normalize_in_process_research_config(InProcessResearchConfig& config,
                                          std::string& rejection_reason) noexcept {
    rejection_reason.clear();
    if (!config.research_enabled) {
        rejection_reason = "Research is disabled.";
        return false;
    }
    if (!is_supported_in_process_experiment_id(config.experiment_id)) {
        rejection_reason = "Unknown in-process research experiment ID.";
        return false;
    }
    if (config.experiment_version != kGGameStateInProcessExperimentVersion) {
        rejection_reason = "Unsupported experiment version.";
        return false;
    }
    if (!validate_research_capability(config.capability, rejection_reason)) {
        return false;
    }
    if (config.expected_sha256 == nullptr || config.expected_sha256[0] == '\0') {
        config.expected_sha256 = kResearchAllowlistedSha256;
    }
    if (!is_research_allowlisted_sha256(config.expected_sha256)) {
        rejection_reason = "Expected SHA-256 is not on the research allowlist.";
        return false;
    }
    if (is_production_supported_sha256(config.expected_sha256)) {
        rejection_reason =
            "Research allowlist entry must not imply production supported registry membership.";
        return false;
    }
    if (config.interval_ms == 0) {
        config.interval_ms = kInProcessResearchDefaultIntervalMs;
    }
    if (config.max_samples == 0) {
        config.max_samples = kInProcessResearchDefaultMaxSamples;
    }
    if (config.interval_ms < kInProcessResearchMinIntervalMs) {
        rejection_reason = "Sample interval is below the minimum bound.";
        return false;
    }
    if (config.interval_ms > kInProcessResearchMaxIntervalMs) {
        config.interval_ms = kInProcessResearchMaxIntervalMs;
    }
    if (config.max_samples > kInProcessResearchMaxSamplesCap) {
        config.max_samples = kInProcessResearchMaxSamplesCap;
    }
    return true;
}

bool transition_research_substate(ResearchSubstate current,
                                  ResearchSubstate next,
                                  bool& valid) noexcept {
    valid = false;
    switch (current) {
    case ResearchSubstate::Disabled:
        valid = (next == ResearchSubstate::Armed || next == ResearchSubstate::Failed);
        break;
    case ResearchSubstate::Armed:
        valid = (next == ResearchSubstate::Observing || next == ResearchSubstate::Stopped
                 || next == ResearchSubstate::Failed);
        break;
    case ResearchSubstate::Observing:
        valid = (next == ResearchSubstate::Stopping || next == ResearchSubstate::Failed);
        break;
    case ResearchSubstate::Stopping:
        valid = (next == ResearchSubstate::Stopped || next == ResearchSubstate::Failed);
        break;
    case ResearchSubstate::Stopped:
        valid = (next == ResearchSubstate::Armed || next == ResearchSubstate::Disabled
                 || next == ResearchSubstate::Failed);
        break;
    case ResearchSubstate::Failed:
        valid = (next == ResearchSubstate::Disabled || next == ResearchSubstate::Stopped);
        break;
    }
    return valid;
}

bool verify_same_process_module_identity(const SameProcessModuleIdentity& identity,
                                         std::string& failure_reason) {
    failure_reason.clear();
    if (identity.module_image == nullptr || identity.module_image_size < 0x1000) {
        failure_reason = "Module image buffer is too small for identity verification.";
        return false;
    }
    if (identity.executable_path.empty()) {
        failure_reason = "Executable path is required for same-process identity.";
        return false;
    }
    if (identity.sha256_hex.empty() || identity.sha256_hex.size() != 64) {
        failure_reason = "Module SHA-256 is required.";
        return false;
    }
    if (!is_research_allowlisted_sha256(identity.sha256_hex)) {
        failure_reason = "Module SHA-256 is not research-allowlisted.";
        return false;
    }
    if (!identity.skip_environment_preflight) {
        const EnvironmentPreflightResult preflight =
            detail::scan_game_executable_directory(identity.executable_path);
        if (!preflight.clean) {
            failure_reason = preflight.message.empty() ? "Environment preflight failed."
                                                       : preflight.message;
            return false;
        }
    }
    return true;
}

InProcessResearchController::InProcessResearchController() = default;

InProcessResearchController::~InProcessResearchController() {
    stop_requested_.store(true, std::memory_order_release);
    join_sampler_if_needed();
}

ResearchSubstate InProcessResearchController::substate() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return substate_;
}

InProcessResearchStatus InProcessResearchController::status_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    InProcessResearchStatus status{};
    status.substate = substate_;
    status.message = message_;
    status.classification = classification_;
    status.samples_collected = samples_.size();
    status.derivation = derivation_;
    status.samples = samples_;
    return status;
}

bool InProcessResearchController::configure(const InProcessResearchConfig& config,
                                            std::string& message_out) {
    join_sampler_if_needed();
    std::lock_guard<std::mutex> lock(mutex_);
    if (substate_ != ResearchSubstate::Disabled && substate_ != ResearchSubstate::Stopped
        && substate_ != ResearchSubstate::Failed) {
        message_out = "Configure rejected: research is active.";
        return false;
    }
    if (!config.research_enabled) {
        config_ = config;
        substate_ = ResearchSubstate::Disabled;
        message_out = "In-process research remains disabled.";
        return true;
    }
    InProcessResearchConfig normalized = config;
    if (!normalize_in_process_research_config(normalized, message_out)) {
        return false;
    }
    config_ = normalized;
    samples_.clear();
    consecutive_read_failures_ = 0;
    substate_ = ResearchSubstate::Disabled;
    message_out = "In-process research configuration accepted.";
    return true;
}

bool InProcessResearchController::arm(const SameProcessModuleIdentity& identity,
                                      std::string& message_out) {
    join_sampler_if_needed();
    std::lock_guard<std::mutex> lock(mutex_);
    if (!config_.research_enabled) {
        message_out = "Arm rejected: research is not enabled.";
        return false;
    }
    if (substate_ != ResearchSubstate::Disabled && substate_ != ResearchSubstate::Stopped) {
        message_out = "Arm rejected: invalid substate for arming.";
        return false;
    }
    if (!verify_same_process_module_identity(identity, message_out)) {
        set_failed_locked(message_out);
        return false;
    }
    if (config_.expected_sha256 != nullptr
        && identity.sha256_hex != config_.expected_sha256) {
        message_out = "Arm rejected: module SHA-256 does not match configured expectation.";
        set_failed_locked(message_out);
        return false;
    }
    identity_ = identity;
    samples_.clear();
    consecutive_read_failures_ = 0;
    classification_ = InProcessSignalClassification::None;
    derivation_ = {};
    derived_address_ = 0;
    if (!derive_address_locked(message_out)) {
        set_failed_locked(message_out);
        return false;
    }
    bool valid = false;
    if (!transition_research_substate(substate_, ResearchSubstate::Armed, valid) || !valid) {
        message_out = "Arm rejected: invalid lifecycle transition.";
        return false;
    }
    substate_ = ResearchSubstate::Armed;
    message_out = "In-process research armed.";
    return true;
}

bool InProcessResearchController::begin_observation(std::string& message_out) {
    join_sampler_if_needed();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (substate_ != ResearchSubstate::Armed) {
            message_out = "Begin rejected: research is not ARMED.";
            return false;
        }
        if (sampler_thread_handle_ != nullptr) {
            message_out = "Begin rejected: sampler already active.";
            return false;
        }
        bool valid = false;
        if (!transition_research_substate(substate_, ResearchSubstate::Observing, valid)
            || !valid) {
            message_out = "Begin rejected: invalid lifecycle transition.";
            return false;
        }
        stop_requested_.store(false, std::memory_order_release);
        substate_ = ResearchSubstate::Observing;
    }

    // Start CRT-aware sampler only AFTER releasing mutex_ so the worker is not
    // blocked behind this call stack.
    const uintptr_t thread =
        _beginthreadex(nullptr, 0, &sampler_thread_trampoline, this, 0, nullptr);
    if (thread == 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        substate_ = ResearchSubstate::Armed;
        message_out = "Begin rejected: failed to start CRT sampler thread.";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sampler_thread_handle_ = reinterpret_cast<void*>(thread);
        message_out = "Observation started.";
    }
    return true;
}

bool InProcessResearchController::begin_observation_on_calling_thread(std::string& message_out) {
    join_sampler_if_needed();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (substate_ != ResearchSubstate::Armed) {
            message_out = "Begin rejected: research is not ARMED.";
            return false;
        }
        if (sampler_thread_handle_ != nullptr) {
            message_out = "Begin rejected: sampler already active.";
            return false;
        }
        bool valid = false;
        if (!transition_research_substate(substate_, ResearchSubstate::Observing, valid)
            || !valid) {
            message_out = "Begin rejected: invalid lifecycle transition.";
            return false;
        }
        stop_requested_.store(false, std::memory_order_release);
        substate_ = ResearchSubstate::Observing;
        message_out = "Observation started on calling thread.";
    }
    execute_sampler_loop();
    return true;
}

bool InProcessResearchController::stop_observation(std::string& message_out) {
    stop_requested_.store(true, std::memory_order_release);
    join_sampler_if_needed();
    std::lock_guard<std::mutex> lock(mutex_);
    if (substate_ == ResearchSubstate::Observing || substate_ == ResearchSubstate::Stopping) {
        substate_ = ResearchSubstate::Stopped;
        message_out = "Observation stopped.";
        return true;
    }
    if (substate_ == ResearchSubstate::Armed) {
        substate_ = ResearchSubstate::Stopped;
        message_out = "Armed research cancelled.";
        return true;
    }
    message_out = "Stop ignored: research is not observing.";
    return substate_ == ResearchSubstate::Stopped;
}

bool InProcessResearchController::shutdown(std::string& message_out) {
    stop_requested_.store(true, std::memory_order_release);
    join_sampler_if_needed();
    std::lock_guard<std::mutex> lock(mutex_);
    if (substate_ == ResearchSubstate::Observing) {
        substate_ = ResearchSubstate::Stopping;
        substate_ = ResearchSubstate::Stopped;
    }
    if (substate_ == ResearchSubstate::Armed) {
        substate_ = ResearchSubstate::Stopped;
    }
    config_.research_enabled = false;
    if (substate_ != ResearchSubstate::Failed) {
        substate_ = ResearchSubstate::Disabled;
    }
    message_out = "In-process research shutdown complete.";
    return true;
}

bool InProcessResearchController::sample_once_for_test(std::string& message_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (substate_ != ResearchSubstate::Observing && substate_ != ResearchSubstate::Armed) {
        message_out = "Sample rejected: invalid substate.";
        return false;
    }
    if (samples_.size() >= config_.max_samples) {
        message_out = "Sample rejected: sample limit reached.";
        return false;
    }
    if (!collect_sample_locked(message_out)) {
        return false;
    }
    if (samples_.size() >= config_.max_samples) {
        substate_ = ResearchSubstate::Stopped;
        message_out = "Sample collected; sample limit reached.";
    } else {
        message_out = "Sample collected.";
    }
    return true;
}

bool InProcessResearchController::derive_address_locked(std::string& message_out) {
    if (!resolve_ggame_state_from_module_image(
            identity_.module_image,
            identity_.module_image_size,
            derivation_)) {
        message_out = derivation_.message.empty()
            ? std::string("gGameState derivation failed.")
            : derivation_.message;
        return false;
    }
    derived_address_ = identity_.module_base + derivation_.global_rva;
    message_out = "gGameState address derived.";
    return true;
}

bool InProcessResearchController::collect_sample_locked(std::string& message_out) {
    research_evidence_append_lifecycle("SAMPLE_BEGIN", "");
    InProcessResearchEvidenceSample sample{};
    sample.sample_index = static_cast<std::uint32_t>(samples_.size());
    sample.monotonic_time_ms = monotonic_time_ms();
    sample.experiment_id = config_.experiment_id;
    sample.experiment_version = config_.experiment_version;
    sample.capability = config_.capability;
    sample.expected_sha256 =
        config_.expected_sha256 != nullptr ? config_.expected_sha256 : "";
    sample.observed_sha256 = identity_.sha256_hex;
    sample.substate = substate_;
    sample.derivation_method = derivation_.method;
    sample.derivation_resolved = derivation_.resolved;
    sample.derived_global_rva = derivation_.global_rva;

    const PassiveReadResult read = passive_read_byte(reinterpret_cast<const void*>(derived_address_));
    sample.read_ok = read.ok;
    sample.raw_byte = read.byte;
    sample.failure_reason = read.failure_reason;

    if (read.ok) {
        research_evidence_append_lifecycle("SAMPLE_READ_OK", "");
        consecutive_read_failures_ = 0;
        if (samples_.empty()) {
            classification_ = InProcessSignalClassification::Insufficient;
        }
    } else {
        research_evidence_append_lifecycle("SAMPLE_READ_FAILED", read.failure_reason.c_str());
        ++consecutive_read_failures_;
        if (consecutive_read_failures_ >= kInProcessResearchMaxConsecutiveReadFailures) {
            message_out = "Repeated read failures exceeded threshold.";
            set_failed_locked(message_out);
            samples_.push_back(std::move(sample));
            research_evidence_append_sample(samples_.back());
            research_evidence_append_lifecycle("SAMPLE_EVIDENCE_WRITTEN", "failed_threshold");
            return false;
        }
    }

    samples_.push_back(std::move(sample));
    research_evidence_append_sample(samples_.back());
    research_evidence_append_lifecycle("SAMPLE_EVIDENCE_WRITTEN", "");
    message_out = "Sample recorded.";
    return true;
}

void InProcessResearchController::sampler_thread_main() {
    while (!stop_requested_.load(std::memory_order_acquire)) {
        std::string message;
        std::uint32_t interval_ms = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            interval_ms = config_.interval_ms;
            if (substate_ != ResearchSubstate::Observing) {
                break;
            }
            const std::uint32_t sample_cap = config_.max_samples;
            if (samples_.size() > sample_cap) {
                message = "Sample count exceeded the configured maximum.";
                set_failed_locked(message);
                research_evidence_append_lifecycle("SAMPLER_STOPPED", "cap_violation");
                research_evidence_end_session(substate_, classification_, message);
                research_activation_status_publish_controller(
                    static_cast<std::uint32_t>(ResearchIntentState::Consumed),
                    static_cast<std::uint32_t>(substate_),
                    static_cast<std::uint32_t>(samples_.size()),
                    false);
                stop_requested_.store(true, std::memory_order_release);
                break;
            }
            if (samples_.size() >= sample_cap) {
                research_evidence_append_lifecycle("SAMPLER_STOP_BEGIN", "max_samples_precheck");
                substate_ = ResearchSubstate::Stopping;
                substate_ = ResearchSubstate::Stopped;
                research_evidence_append_lifecycle("SAMPLER_STOPPED", "max_samples_precheck");
                research_evidence_end_session(substate_,
                                              classification_,
                                              "Max sample bound reached.");
                research_activation_status_publish_controller(
                    static_cast<std::uint32_t>(ResearchIntentState::Consumed),
                    static_cast<std::uint32_t>(substate_),
                    static_cast<std::uint32_t>(samples_.size()),
                    false);
                stop_requested_.store(true, std::memory_order_release);
                break;
            }
            if (!collect_sample_locked(message)) {
                if (substate_ == ResearchSubstate::Failed) {
                    research_evidence_append_lifecycle("SAMPLER_STOP_BEGIN", "failed");
                    research_evidence_append_lifecycle("SAMPLER_STOPPED", "failed");
                    research_evidence_end_session(substate_,
                                                  classification_,
                                                  message.empty() ? "Observation failed." : message);
                    research_activation_status_publish_controller(
                        static_cast<std::uint32_t>(ResearchIntentState::Consumed),
                        static_cast<std::uint32_t>(substate_),
                        static_cast<std::uint32_t>(samples_.size()),
                        false);
                }
                break;
            }
            if (samples_.size() >= sample_cap) {
                research_evidence_append_lifecycle("SAMPLER_STOP_BEGIN", "max_samples");
                substate_ = ResearchSubstate::Stopping;
                substate_ = ResearchSubstate::Stopped;
                research_evidence_append_lifecycle("SAMPLER_STOPPED", "max_samples");
                research_evidence_end_session(substate_,
                                              classification_,
                                              "Max sample bound reached.");
                research_activation_status_publish_controller(
                    static_cast<std::uint32_t>(ResearchIntentState::Consumed),
                    static_cast<std::uint32_t>(substate_),
                    static_cast<std::uint32_t>(samples_.size()),
                    false);
                stop_requested_.store(true, std::memory_order_release);
                break;
            }
            research_activation_status_publish_controller(
                static_cast<std::uint32_t>(ResearchIntentState::Consumed),
                static_cast<std::uint32_t>(substate_),
                static_cast<std::uint32_t>(samples_.size()),
                false);
        }
        research_evidence_append_lifecycle("SAMPLE_WAIT_BEGIN", "");
        sleep_ms(interval_ms);
        research_evidence_append_lifecycle("SAMPLE_WAIT_END", "");
    }
}

void InProcessResearchController::join_sampler_if_needed() {
    void* handle = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        handle = sampler_thread_handle_;
        sampler_thread_handle_ = nullptr;
    }
    if (handle == nullptr) {
        return;
    }
    const HANDLE win_handle = static_cast<HANDLE>(handle);
    if (GetCurrentThreadId() == GetThreadId(win_handle)) {
        std::lock_guard<std::mutex> lock(mutex_);
        sampler_thread_handle_ = handle;
        return;
    }
    WaitForSingleObject(win_handle, INFINITE);
    CloseHandle(win_handle);
}

bool InProcessResearchController::sampler_active() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return sampler_thread_handle_ != nullptr;
}

void InProcessResearchController::set_failed_locked(const std::string& reason) {
    message_ = reason;
    classification_ = InProcessSignalClassification::None;
    substate_ = ResearchSubstate::Failed;
}

std::uint64_t InProcessResearchController::monotonic_time_ms() const noexcept {
    return steady_ms();
}

namespace detail {

bool in_process_research_test_sample_once(InProcessResearchController& controller,
                                          std::string& message_out) {
    return controller.sample_once_for_test(message_out);
}

bool in_process_research_test_force_derived_address(InProcessResearchController& controller,
                                                    std::uintptr_t address) {
    controller.derived_address_ = address;
    return true;
}

bool in_process_research_test_wait_until_observing(InProcessResearchController& controller,
                                                   std::uint32_t timeout_ms) {
    const auto deadline = steady_ms() + timeout_ms;
    while (steady_ms() < deadline) {
        if (controller.substate() == ResearchSubstate::Observing) {
            const auto status = controller.status_snapshot();
            if (status.samples_collected >= 1) {
                return true;
            }
        }
        if (controller.substate() == ResearchSubstate::Stopped
            || controller.substate() == ResearchSubstate::Failed) {
            return controller.substate() == ResearchSubstate::Observing;
        }
        sleep_ms(10);
    }
    return controller.substate() == ResearchSubstate::Observing
        && controller.status_snapshot().samples_collected >= 1;
}

bool in_process_research_test_wait_until_substate(InProcessResearchController& controller,
                                                  ResearchSubstate expected,
                                                  std::uint32_t timeout_ms) {
    const auto deadline = steady_ms() + timeout_ms;
    while (steady_ms() < deadline) {
        if (controller.substate() == expected) {
            return true;
        }
        sleep_ms(10);
    }
    return controller.substate() == expected;
}

bool in_process_research_test_sampler_thread_joinable(InProcessResearchController& controller) {
    return controller.sampler_active();
}

} // namespace detail

} // namespace deflux::game::gta_sa_de
