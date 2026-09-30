#pragma once

// Task #7B.1: research-only read-only live candidate observation.
// Not invoked by normal runtime qualification, readiness, or supported-build policy.

#include "deflux/core/process_lifecycle.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace deflux::game::gta_sa_de {

inline constexpr const char kLiveObservationTargetSha256[] =
    "63e633bb51c35fe35ca98f503b62e7ddb8403abe4c9d501cfa9162892453846f";

inline constexpr std::array<std::uint32_t, 2> kArchitectApprovedCandidateRvas = {
    0x05391B70u,
    0x05764130u,
};

inline constexpr std::uint32_t kLiveObservationDefaultIntervalMs = 500;
inline constexpr std::uint32_t kLiveObservationDefaultMaxSamples = 60;
inline constexpr std::uint32_t kLiveObservationMaxIntervalMs = 5000;
inline constexpr std::uint32_t kLiveObservationMaxSamples = 120;

enum class LiveObservationPhase : std::uint8_t {
    Unknown = 0,
    Menu,
    Loading,
    GameplayStanding,
    GameplayMoving,
    PauseMenu,
};

enum class LiveObservationStatus : std::uint8_t {
    Ok = 0,
    NotRunning,
    BuildMismatch,
    ProcessChanged,
    ModuleInvalid,
    EnvironmentContaminated,
    Error,
};

enum class LiveObservationConsistency : std::uint8_t {
    Consistent = 0,
    Inconsistent,
    InsufficientData,
};

struct LiveObservationModuleRange {
    std::uint64_t base = 0;
    std::uint64_t size = 0;
};

struct LiveObservationSample {
    std::uint32_t sample_index = 0;
    std::uint64_t timestamp_ms = 0;
    LiveObservationPhase phase = LiveObservationPhase::Unknown;
    std::uint32_t candidate_rva = 0;
    std::uint64_t module_base = 0;
    std::uint64_t slot_virtual_address = 0;
    bool slot_read_ok = false;
    std::uint64_t slot_value = 0;
    bool slot_is_null = false;
    bool slot_is_canonical = false;
    bool object_probe_read_ok = false;
    std::uint64_t first_pointer_value = 0;
    bool first_value_readable = false;
    bool first_value_in_module = false;
    std::uint64_t first_value_module_base = 0;
};

struct LiveObservationPhaseSummary {
    LiveObservationPhase phase = LiveObservationPhase::Unknown;
    std::size_t samples = 0;
    std::size_t read_successes = 0;
    std::size_t null_samples = 0;
    std::size_t non_null_samples = 0;
    std::size_t distinct_pointer_values = 0;
    bool pointer_stable = false;
    std::size_t first_value_in_module_samples = 0;
    LiveObservationConsistency consistency = LiveObservationConsistency::InsufficientData;
};

struct LiveObservationCandidateSummary {
    std::uint32_t candidate_rva = 0;
    std::size_t samples = 0;
    std::size_t read_successes = 0;
    std::size_t null_samples = 0;
    std::size_t non_null_samples = 0;
    std::size_t distinct_pointer_values = 0;
    bool pointer_stable = false;
    std::size_t first_value_in_module_samples = 0;
    LiveObservationConsistency consistency = LiveObservationConsistency::InsufficientData;
    std::vector<LiveObservationPhaseSummary> by_phase;
};

struct LiveObservationConfig {
    const char* expected_sha256 = kLiveObservationTargetSha256;
    std::uint32_t interval_ms = kLiveObservationDefaultIntervalMs;
    std::uint32_t max_samples = kLiveObservationDefaultMaxSamples;
};

struct LiveObservationSessionResult {
    LiveObservationStatus status = LiveObservationStatus::NotRunning;
    std::string message;
    std::uint32_t process_id = 0;
    std::uint64_t module_base = 0;
    std::uint64_t module_size = 0;
    std::size_t samples_collected = 0;
    std::vector<LiveObservationSample> samples;
    std::vector<LiveObservationCandidateSummary> summaries;
};

using LiveObservationReadFn =
    std::function<bool(std::uint64_t address, void* out, std::size_t bytes)>;

struct LiveObservationProbeContext {
    std::uint64_t module_base = 0;
    std::uint64_t module_size = 0;
    std::vector<LiveObservationModuleRange> module_ranges;
};

bool is_architect_approved_candidate_rva(std::uint32_t candidate_rva) noexcept;

bool sha256_matches_live_observation_target(std::string_view sha256_hex) noexcept;

bool process_identity_matches(const deflux::core::ProcessInstance& expected,
                              const deflux::core::ProcessInstance& observed) noexcept;

LiveObservationPhase parse_live_observation_phase(std::string_view label) noexcept;

const char* to_string(LiveObservationPhase phase) noexcept;
const char* to_string(LiveObservationStatus status) noexcept;
const char* to_string(LiveObservationConsistency consistency) noexcept;

bool address_in_loaded_module(std::uint64_t address,
                              const std::vector<LiveObservationModuleRange>& ranges,
                              std::uint64_t& module_base_out) noexcept;

LiveObservationSample probe_candidate_slot(std::uint32_t candidate_rva,
                                             const LiveObservationProbeContext& context,
                                             const LiveObservationReadFn& read,
                                             LiveObservationPhase phase,
                                             std::uint32_t sample_index,
                                             std::uint64_t timestamp_ms) noexcept;

LiveObservationPhaseSummary summarize_live_observation_phase(
    std::uint32_t candidate_rva,
    LiveObservationPhase phase,
    const std::vector<LiveObservationSample>& samples) noexcept;

LiveObservationCandidateSummary summarize_live_observation_candidate(
    std::uint32_t candidate_rva,
    const std::vector<LiveObservationSample>& samples) noexcept;

LiveObservationConsistency classify_live_observation_consistency(
    std::size_t samples,
    std::size_t read_successes,
    std::size_t distinct_pointer_values) noexcept;

bool normalize_live_observation_config(LiveObservationConfig& config) noexcept;

// Interactive session: calls next_phase before each sample batch. Returning false stops cleanly.
LiveObservationSessionResult run_live_candidate_observation_session(
    const deflux::core::ProcessObservation& observation,
    LiveObservationConfig config,
    const std::function<bool(LiveObservationPhase& phase_out)>& next_phase);

// Task #7B.2: bounded structural identity probe (research-only, 0x05764130 only).

inline constexpr std::uint32_t kStructuralIdentityProbeCandidateRva = 0x05764130u;
inline constexpr std::size_t kStructuralObjectPrefixBytes = 256;
inline constexpr std::size_t kStructuralPointerFieldSize = 8;
inline constexpr std::size_t kStructuralMaxPointerFields =
    kStructuralObjectPrefixBytes / kStructuralPointerFieldSize;

enum class StructureIdentityClassification : std::uint8_t {
    Observed = 0,
    Insufficient,
    Contradicted,
};

struct StructuralIdentityFieldProbe {
    std::uint32_t offset = 0;
    std::uint64_t raw_value = 0;
    bool is_null = false;
    bool is_canonical = false;
    bool in_loaded_module = false;
    std::uint64_t module_base = 0;
    bool target_readable = false;
    bool dereference_attempted = false;
    std::uint64_t dereferenced_value = 0;
};

struct StructuralIdentitySnapshot {
    std::uint32_t candidate_rva = 0;
    std::uint64_t slot_virtual_address = 0;
    bool slot_read_ok = false;
    std::uint64_t object_virtual_address = 0;
    bool object_is_null = false;
    bool object_prefix_read_ok = false;
    std::size_t object_prefix_bytes_read = 0;
    std::uint64_t first_qword = 0;
    bool first_qword_in_module = false;
    std::uint64_t first_qword_module_base = 0;
    std::vector<StructuralIdentityFieldProbe> fields;
    StructureIdentityClassification classification = StructureIdentityClassification::Insufficient;
    std::string classification_rationale;
    std::vector<std::string> hypothesis_notes;
    std::vector<std::string> correlation_notes;
};

struct StructuralIdentityProbeResult {
    LiveObservationStatus status = LiveObservationStatus::NotRunning;
    std::string message;
    StructuralIdentitySnapshot snapshot{};
};

bool is_structural_identity_probe_candidate(std::uint32_t candidate_rva) noexcept;

const char* to_string(StructureIdentityClassification classification) noexcept;

StructureIdentityClassification classify_structure_identity(
    const StructuralIdentitySnapshot& snapshot) noexcept;

StructuralIdentitySnapshot probe_candidate_structure(
    std::uint32_t candidate_rva,
    const LiveObservationProbeContext& context,
    const LiveObservationReadFn& read) noexcept;

StructuralIdentityProbeResult run_structural_identity_probe(
    const deflux::core::ProcessObservation& observation);

// One deterministic line per captured field probe (no filtering or truncation).
std::vector<std::string> format_structural_identity_field_lines(
    const StructuralIdentitySnapshot& snapshot);

} // namespace deflux::game::gta_sa_de
