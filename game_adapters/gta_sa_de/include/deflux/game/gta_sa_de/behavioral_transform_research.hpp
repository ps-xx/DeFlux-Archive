#pragma once

// Task #11A: bounded behavioral transform discovery. Research only.
// Not local-player identity. Not a production transform. Read-only.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace deflux::game::gta_sa_de {

inline constexpr const char kBehavioralTransformExperimentId[] =
    "GTA_SA_DE_BEHAVIORAL_TRANSFORM_V1";

inline constexpr std::uint32_t kBehavioralMaxRegionsConsidered = 100000;
inline constexpr std::uint32_t kBehavioralMaxRegionsAccepted = 16384;
inline constexpr std::uint64_t kBehavioralMaxBytesSampled = 64ull * 1024ull * 1024ull;
inline constexpr std::uint32_t kBehavioralMaxCandidates = 4096;
inline constexpr std::uint32_t kBehavioralMaxClustersPerPage = 8;
inline constexpr std::uint32_t kBehavioralMaxCaptureMs = 15000;
inline constexpr std::uint32_t kBehavioralMarkerWaitMs = 30u * 60u * 1000u;
inline constexpr std::uint32_t kBehavioralMinActionMs = 3000;
inline constexpr std::uint32_t kBehavioralMinSettleMs = 3000;
inline constexpr std::uint32_t kBehavioralCoverageStrategyVersion = 2;
inline constexpr std::uint32_t kBehavioralCoverageStripeBytes = 4096;
inline constexpr float kBehavioralStandingTolerance = 1.5f;
inline constexpr float kBehavioralMinMoveDistance = 1.0f;
inline constexpr float kBehavioralMaxDisplacement = 500.0f;
inline constexpr float kBehavioralAbsurdMagnitude = 1000000.0f;

// Win32 memory constants, duplicated so this header stays free of Windows.h.
inline constexpr std::uint32_t kResearchMemCommit = 0x1000u;
inline constexpr std::uint32_t kResearchMemPrivate = 0x20000u;
inline constexpr std::uint32_t kResearchMemMapped = 0x40000u;
inline constexpr std::uint32_t kResearchMemImage = 0x1000000u;
inline constexpr std::uint32_t kResearchPageNoAccess = 0x01u;
inline constexpr std::uint32_t kResearchPageReadOnly = 0x02u;
inline constexpr std::uint32_t kResearchPageReadWrite = 0x04u;
inline constexpr std::uint32_t kResearchPageWriteCopy = 0x08u;
inline constexpr std::uint32_t kResearchPageExecute = 0x10u;
inline constexpr std::uint32_t kResearchPageGuard = 0x100u;

enum class BehavioralProtocol : std::uint32_t {
    Primary = 1,
    Validation = 2,
};

enum class PhaseStage : std::uint32_t {
    ActionStart = 1,
    ActionEnd = 2,
    Capture = 3,
};

enum class PhaseIntervalStatus : std::uint32_t {
    Accept = 0,
    TooShort,
    EndBeforeStart,
};

enum class CandidateClass : std::uint32_t {
    Unclassified = 0,
    BehavioralStrong,
    BehavioralModerate,
    BehavioralWeak,
    CameraLike,
    Transient,
    Rejected,
};

struct RegionDescriptor {
    std::uint64_t base = 0;
    std::uint64_t size = 0;
    std::uint32_t state = 0;
    std::uint32_t protect = 0;
    std::uint32_t type = 0;
};

struct RegionDecision {
    bool accept = false;
    const char* reason = "";
};

struct ScanBudget {
    std::uint32_t regions_considered = 0;
    std::uint32_t regions_accepted = 0;
    std::uint64_t bytes_considered = 0;
    std::uint64_t bytes_sampled = 0;
    std::uint32_t candidates_before_filter = 0;
    std::uint32_t candidates_after_filter = 0;
    std::uint32_t dense_pages_rejected = 0;
    std::uint32_t elapsed_ms = 0;
    bool aborted = false;
    bool coverage_partial = false;
    const char* abort_reason = "";
    std::uint32_t coverage_strategy_version = kBehavioralCoverageStrategyVersion;
    std::uint32_t coverage_window_index = 0;
    std::uint32_t coverage_window_count = 0;
    std::uint32_t regions_sampled = 0;
    std::uint64_t total_eligible_bytes = 0;
    std::uint64_t eligible_bytes_before_window = 0;
    std::uint64_t coverage_window_start = 0;
    std::uint64_t coverage_window_end = 0;
};

struct EligibleSpan {
    std::uint64_t base = 0;
    std::uint64_t size = 0;
};

struct CoverageSlice {
    std::uint64_t base = 0;
    std::uint64_t size = 0;
    std::uint64_t region_base = 0;
};

struct CoverageSelection {
    std::uint32_t strategy_version = kBehavioralCoverageStrategyVersion;
    std::uint32_t window_index = 0;
    std::uint32_t window_count = 0;
    std::uint32_t stripe_bytes = kBehavioralCoverageStripeBytes;
    std::uint32_t regions_selected = 0;
    std::uint64_t total_eligible_bytes = 0;
    std::uint64_t eligible_bytes_before_window = 0;
    std::uint64_t window_start = 0;
    std::uint64_t window_end = 0;
    std::uint64_t bytes_selected = 0;
    bool partial = false;
    bool addresses_comparable_across_processes = false;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct PhaseObservation {
    std::uint64_t session_nonce = 0;
    std::uint32_t sequence = 0;
    std::uint64_t timestamp_ms = 0;
    std::string phase;
    Vec3 value{};
    bool present = false;
};

struct TransformCandidate {
    std::uint64_t address = 0;
    std::uint64_t region_base = 0;
    std::vector<PhaseObservation> observations;
    CandidateClass classification = CandidateClass::Unclassified;
};

struct BehaviorSignature {
    float walk_distance = 0.0f;
    float backward_alignment = 0.0f;
    float turn_distance = 0.0f;
    float run_distance = 0.0f;
    bool valid = false;
};

struct PhaseMarker {
    std::uint64_t session_nonce = 0;
    std::uint32_t sequence = 0;
    std::uint64_t timestamp_ms = 0;
    BehavioralProtocol protocol = BehavioralProtocol::Primary;
    PhaseStage stage = PhaseStage::ActionStart;
    std::uint32_t coverage_window = 0;
    std::string phase;
};

RegionDecision classify_research_region(const RegionDescriptor& region) noexcept;

bool note_research_region(ScanBudget& budget, const RegionDescriptor& region) noexcept;

bool consume_sample_bytes(ScanBudget& budget, std::uint64_t bytes) noexcept;

bool plausible_position(Vec3 value) noexcept;

float position_distance(Vec3 a, Vec3 b) noexcept;

const char* behavioral_phase_at(BehavioralProtocol protocol, std::uint32_t index) noexcept;

std::uint32_t behavioral_phase_count(BehavioralProtocol protocol) noexcept;

bool behavioral_phase_in_order(BehavioralProtocol protocol,
                               std::uint32_t sequence,
                               const char* phase_name) noexcept;

bool marker_matches_session(const PhaseMarker& marker, std::uint64_t session_nonce) noexcept;

bool behavioral_phase_is_movement(const char* phase_name) noexcept;

std::uint32_t behavioral_phase_minimum_ms(const char* phase_name) noexcept;

PhaseIntervalStatus validate_phase_interval(const char* phase_name,
                                            std::uint64_t action_start_ms,
                                            std::uint64_t action_end_ms) noexcept;

bool phase_stage_follows(PhaseStage previous, PhaseStage next) noexcept;

bool candidate_observations_ready(const TransformCandidate& candidate) noexcept;

bool positional_series_is_two_state(const TransformCandidate& candidate) noexcept;

bool select_stratified_coverage_window(const EligibleSpan* spans,
                                       std::size_t span_count,
                                       std::uint32_t window_index,
                                       std::uint64_t byte_budget,
                                       std::uint32_t stripe_bytes,
                                       CoverageSelection& selection,
                                       std::vector<CoverageSlice>& slices);

std::vector<TransformCandidate> extract_changed_position_triples(
    const std::uint8_t* before,
    const std::uint8_t* after,
    std::size_t size,
    std::uint64_t base_address,
    std::uint64_t region_base,
    ScanBudget& budget);

bool observe_candidate_phase(TransformCandidate& candidate,
                             const PhaseMarker& marker,
                             bool present,
                             Vec3 value);

void classify_transform_candidate(TransformCandidate& candidate);

BehaviorSignature behavior_signature(const TransformCandidate& candidate) noexcept;

enum class CrossSessionCorrelation : std::uint32_t {
    NoMatch = 0,
    UniqueBehavioralMatch,
    Ambiguous,
};

CrossSessionCorrelation correlate_behavior_across_sessions(
    const std::vector<BehaviorSignature>& previous,
    const std::vector<BehaviorSignature>& current) noexcept;

const char* to_string(CandidateClass classification) noexcept;

void run_behavioral_transform_session(std::uint64_t session_nonce,
                                      std::uint32_t process_id,
                                      std::uint64_t creation_identity,
                                      const char* expected_sha256,
                                      const char* observed_sha256) noexcept;

bool publish_behavioral_phase_marker(const PhaseMarker& marker, std::string& message) noexcept;

bool wait_for_behavioral_phase_ack(std::uint64_t session_nonce,
                                   std::uint32_t sequence,
                                   PhaseStage stage,
                                   std::uint32_t timeout_ms,
                                   std::string& message) noexcept;

int run_behavioral_phase_console(std::uint64_t session_nonce,
                                 BehavioralProtocol protocol,
                                 std::uint32_t coverage_window) noexcept;

} // namespace deflux::game::gta_sa_de
