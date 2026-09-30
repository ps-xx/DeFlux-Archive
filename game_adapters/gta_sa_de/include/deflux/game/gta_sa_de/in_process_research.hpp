#pragma once

// Task #9B: in-process research plumbing (gGameState PASSIVE_READ experiment).
// Not readiness. Not production support. Disabled by default.

#include "deflux/game/gta_sa_de/ggame_state_observation.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace deflux::game::gta_sa_de {

inline constexpr const char kGGameStateInProcessExperimentId[] =
    "GTA_SA_DE_GGAMESTATE_OBSERVATION_V1";
inline constexpr std::uint32_t kGGameStateInProcessExperimentVersion = 1;

inline constexpr std::uint32_t kInProcessResearchDefaultIntervalMs = 500;
inline constexpr std::uint32_t kInProcessResearchDefaultMaxSamples = 120;
inline constexpr std::uint32_t kInProcessResearchMinIntervalMs = 50;
inline constexpr std::uint32_t kInProcessResearchMaxIntervalMs = 5000;
inline constexpr std::uint32_t kInProcessResearchMaxSamplesCap = 120;
inline constexpr std::uint32_t kInProcessResearchMaxConsecutiveReadFailures = 10;

enum class ResearchCapability : std::uint32_t {
    None = 0,
    PassiveRead = 1,
    Write = 2,
    Hook = 4,
};

enum class ResearchSubstate : std::uint8_t {
    Disabled = 0,
    Armed,
    Observing,
    Stopping,
    Stopped,
    Failed,
};

enum class InProcessSignalClassification : std::uint8_t {
    None = 0,
    Insufficient,
    Reproduced,
    Contradicted,
};

struct InProcessResearchConfig {
    bool research_enabled = false;
    const char* experiment_id = nullptr;
    std::uint32_t experiment_version = 0;
    ResearchCapability capability = ResearchCapability::None;
    const char* expected_sha256 = nullptr;
    std::uint32_t interval_ms = kInProcessResearchDefaultIntervalMs;
    std::uint32_t max_samples = kInProcessResearchDefaultMaxSamples;
};

struct SameProcessModuleIdentity {
    const std::uint8_t* module_image = nullptr;
    std::size_t module_image_size = 0;
    std::uintptr_t module_base = 0;
    std::string executable_path;
    std::string sha256_hex;
    bool skip_environment_preflight = false;
};

struct InProcessResearchEvidenceSample {
    std::uint32_t sample_index = 0;
    std::uint64_t monotonic_time_ms = 0;
    const char* experiment_id = "";
    std::uint32_t experiment_version = 0;
    ResearchCapability capability = ResearchCapability::None;
    std::string expected_sha256;
    std::string observed_sha256;
    ResearchSubstate substate = ResearchSubstate::Disabled;
    GameStateAddressDerivation derivation_method = GameStateAddressDerivation::None;
    bool derivation_resolved = false;
    std::uint32_t derived_global_rva = 0;
    bool read_ok = false;
    std::uint8_t raw_byte = 0;
    std::string failure_reason;
};

struct InProcessResearchStatus {
    ResearchSubstate substate = ResearchSubstate::Disabled;
    std::string message;
    InProcessSignalClassification classification = InProcessSignalClassification::None;
    std::size_t samples_collected = 0;
    GameStateAddressResolution derivation{};
    std::vector<InProcessResearchEvidenceSample> samples;
};

const char* to_string(ResearchCapability capability) noexcept;
const char* to_string(ResearchSubstate substate) noexcept;
const char* to_string(InProcessSignalClassification classification) noexcept;

bool is_supported_in_process_experiment_id(const char* experiment_id) noexcept;

bool normalize_in_process_research_config(InProcessResearchConfig& config,
                                          std::string& rejection_reason) noexcept;

bool validate_research_capability(ResearchCapability capability,
                                  std::string& rejection_reason) noexcept;

bool verify_same_process_module_identity(const SameProcessModuleIdentity& identity,
                                         std::string& failure_reason);

bool transition_research_substate(ResearchSubstate current,
                                  ResearchSubstate next,
                                  bool& valid) noexcept;

class InProcessResearchController;

namespace detail {

bool in_process_research_test_sample_once(InProcessResearchController& controller,
                                          std::string& message_out);
bool in_process_research_test_force_derived_address(InProcessResearchController& controller,
                                                    std::uintptr_t address);
bool in_process_research_test_wait_until_observing(InProcessResearchController& controller,
                                                   std::uint32_t timeout_ms);
bool in_process_research_test_wait_until_substate(InProcessResearchController& controller,
                                                  ResearchSubstate expected,
                                                  std::uint32_t timeout_ms);
bool in_process_research_test_sampler_thread_joinable(InProcessResearchController& controller);
} // namespace detail

class InProcessResearchController {
public:
    InProcessResearchController();
    ~InProcessResearchController();

    ResearchSubstate substate() const noexcept;
    InProcessResearchStatus status_snapshot() const;

    bool configure(const InProcessResearchConfig& config, std::string& message_out);
    bool arm(const SameProcessModuleIdentity& identity, std::string& message_out);
    bool begin_observation(std::string& message_out);
    // Synthetic/pipeline-proof only: run the bounded sampler on the calling thread.
    // Live GTA research continues to use begin_observation() (async CRT worker).
    bool begin_observation_on_calling_thread(std::string& message_out);
    bool stop_observation(std::string& message_out);
    bool shutdown(std::string& message_out);

    // Entry for the CRT sampler trampoline (free function in .cpp).
    void execute_sampler_loop() { sampler_thread_main(); }

private:
    friend bool detail::in_process_research_test_sample_once(InProcessResearchController&,
                                                               std::string&);
    friend bool detail::in_process_research_test_force_derived_address(
        InProcessResearchController&,
        std::uintptr_t);
    friend bool detail::in_process_research_test_wait_until_observing(
        InProcessResearchController&,
        std::uint32_t);
    friend bool detail::in_process_research_test_wait_until_substate(InProcessResearchController&,
                                                                     ResearchSubstate,
                                                                     std::uint32_t);
    friend bool detail::in_process_research_test_sampler_thread_joinable(
        InProcessResearchController&);

    bool sample_once_for_test(std::string& message_out);
    bool derive_address_locked(std::string& message_out);
    bool collect_sample_locked(std::string& message_out);
    void sampler_thread_main();
    void join_sampler_if_needed();
    void set_failed_locked(const std::string& reason);
    std::uint64_t monotonic_time_ms() const noexcept;
    bool sampler_active() const noexcept;

    mutable std::mutex mutex_;
    ResearchSubstate substate_ = ResearchSubstate::Disabled;
    InProcessResearchConfig config_{};
    SameProcessModuleIdentity identity_{};
    GameStateAddressResolution derivation_{};
    std::uintptr_t derived_address_ = 0;
    std::vector<InProcessResearchEvidenceSample> samples_;
    std::string message_;
    InProcessSignalClassification classification_ = InProcessSignalClassification::None;
    std::uint32_t consecutive_read_failures_ = 0;

    // CRT-aware sampler thread (_beginthreadex). Never std::thread: destroying a
    // joinable std::thread calls std::terminate/abort, and CreateThread-backed
    // workers are unsafe for CRT use after remote LoadLibrary.
    void* sampler_thread_handle_ = nullptr;
    std::atomic<bool> stop_requested_{false};
};

} // namespace deflux::game::gta_sa_de
