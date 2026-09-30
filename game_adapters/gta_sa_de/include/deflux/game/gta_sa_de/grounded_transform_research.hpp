#pragma once

// Task #14B: CLEO-grounded transform discovery. Research only.
// CLEO is a coordinate oracle. This module does not attach to GTA.
// GROUND_TRUTH_STRONG is a transform candidate, not local-player identity.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace deflux::game::gta_sa_de {

inline constexpr const char kGroundedTransformExperimentId[] =
    "GTA_SA_DE_GROUNDED_TRANSFORM_V1";

inline constexpr std::uint64_t kGroundedMaxBytesConsidered = 4ull * 1024ull * 1024ull * 1024ull;
// Wall-clock budget owned by one scan. It starts when that scan is entered.
// Oracle waiting uses its own clock and does not reduce this budget.
// Offline Debug measurement of the optimized kernel on this machine:
// 4 MiB scanned in 172 ms, all of it match_ms. The 4 GiB consideration cap
// extrapolates to 176128 ms. 360000 ms covers that cap and leaves headroom
// for ReadProcessMemory.
inline constexpr std::uint32_t kGroundedFullScanMs = 360000;
inline constexpr std::uint32_t kGroundedRecheckMs = 15000;
inline constexpr std::uint64_t kGroundedScanTimeSliceBytes = 64ull * 1024ull;
inline constexpr const char kGroundedStageNone[] = "NONE";
inline constexpr const char kGroundedStageEnumeration[] = "ENUMERATION";
inline constexpr const char kGroundedStageRead[] = "READ";
inline constexpr const char kGroundedStageMatch[] = "MATCH";
inline constexpr const char kGroundedStageFilter[] = "FILTER";
inline constexpr const char kGroundedStagePostScan[] = "POST_SCAN";
inline constexpr std::uint32_t kGroundedMaxCandidates = 8192;
inline constexpr std::uint32_t kGroundedDensePageInitial = 8;
inline constexpr std::uint32_t kGroundedDensePageResolved = 3;
inline constexpr std::uint32_t kGroundedWatcherPollMs = 50;
inline constexpr std::uint32_t kGroundedOraclePeriodMs = 200;
inline constexpr std::uint32_t kGroundedStandingWindowMs = 500;
inline constexpr std::uint32_t kGroundedMinPhaseMs = 3000;
inline constexpr double kGroundedExactMeters = 0.001;
inline constexpr double kGroundedNearMeters = 0.05;
inline constexpr double kGroundedStaleGapMeters = 0.5;
inline constexpr double kGroundedTurnMeters = 0.25;
inline constexpr double kGroundedDirectionCosine = 0.95;
inline constexpr double kGroundedMinSeparationMeters = 15.0;
inline constexpr double kGroundedOffsetZ = 1.04;
inline constexpr double kGroundedOracleAbsurd = 100000.0;
inline constexpr double kGroundedCandidateAbsurd = 1000000.0;
inline constexpr std::uint64_t kGroundedPageBytes = 4096;

inline constexpr const char kGroundedLiveClaimWithheld[] = "NO_LIVE_CLAIM";
inline constexpr const char kGroundedNoCandidate[] = "NO_GROUNDED_TRANSFORM_CANDIDATE";

enum class GroundedEvidence : std::uint32_t {
    Unclassified = 0,
    Strong,
    Moderate,
    Weak,
    CleoContamination,
    StaleCopy,
    Rejected,
};

enum class GroundedModuleKind : std::uint32_t {
    GameImage = 0,
    CleoAsi,
    CleoPlugin,
    ResearchModule,
    ResearchBuffer,
};

enum class GroundedSlot : std::uint32_t {
    StandA1 = 0,
    StandA2,
    StandC1,
    StandC2,
    StandE1,
    StandE2,
    TurnF,
    StandH1,
    StandH2,
    Silence,
    Count,
};

struct GroundedVec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct GroundedRegion {
    std::uint64_t base = 0;
    std::uint64_t size = 0;
    std::uint32_t state = 0;
    std::uint32_t protect = 0;
    std::uint32_t type = 0;
    const std::uint8_t* bytes = nullptr;
};

struct GroundedModule {
    std::uint64_t base = 0;
    std::uint64_t size = 0;
    GroundedModuleKind kind = GroundedModuleKind::GameImage;
};

struct GroundedView {
    const GroundedRegion* regions = nullptr;
    std::size_t region_count = 0;
    const GroundedModule* modules = nullptr;
    std::size_t module_count = 0;
};

struct ScanClock {
    std::uint64_t (*now_ms)(void* context) = nullptr;
    void* context = nullptr;
};

struct GroundedHit {
    std::uint64_t address = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double offset_x = 0.0;
    double offset_y = 0.0;
    double offset_z = 0.0;
    bool marker_allocation = false;
    std::uint32_t initial_page_matches = 1;
};

struct StreamScanRequest {
    GroundedView view{};
    GroundedVec3 oracle{};
    ScanClock clock{};
    std::uint64_t byte_limit = kGroundedMaxBytesConsidered;
    std::uint32_t time_limit_ms = kGroundedFullScanMs;
    std::uint32_t candidate_limit = kGroundedMaxCandidates;
};

struct Float64Diagnostic {
    std::uint64_t address = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct GroundedScanTiming {
    std::uint64_t enumeration_ms = 0;
    std::uint32_t eligible_region_count = 0;
    std::uint64_t eligible_bytes = 0;
    std::uint64_t bytes_attempted = 0;
    std::uint64_t bytes_read = 0;
    std::uint64_t read_ms = 0;
    std::uint64_t match_ms = 0;
    std::uint64_t filter_ms = 0;
    std::uint64_t total_scan_ms = 0;
    const char* timeout_stage = kGroundedStageNone;
    std::uint32_t timeout_budget_ms = 0;
    bool partial_scan = false;
};

struct StreamScanResult {
    bool aborted = false;
    const char* abort_reason = "";
    std::uint64_t bytes_considered = 0;
    std::vector<GroundedHit> hits;
    std::vector<Float64Diagnostic> float64_hits;
    GroundedScanTiming timing{};
};

inline bool grounded_axis_within_near(double memory, double oracle) noexcept {
    const double delta = memory - oracle;
    return delta <= kGroundedNearMeters && delta >= -kGroundedNearMeters;
}

// Necessary condition for the frozen offset catalog. Callers still accept only
// through grounded_select_offset.
inline bool grounded_triple_can_match(GroundedVec3 memory, GroundedVec3 oracle) noexcept {
    if (!grounded_axis_within_near(memory.x, oracle.x) || !grounded_axis_within_near(memory.y, oracle.y)) {
        return false;
    }
    return grounded_axis_within_near(memory.z, oracle.z)
        || grounded_axis_within_near(memory.z, oracle.z - kGroundedOffsetZ)
        || grounded_axis_within_near(memory.z, oracle.z + kGroundedOffsetZ);
}

struct RecheckRequest {
    const GroundedHit* hits = nullptr;
    std::size_t hit_count = 0;
    GroundedView view{};
    GroundedVec3 oracle{};
    ScanClock clock{};
    std::uint32_t time_limit_ms = kGroundedRecheckMs;
};

struct RecheckResult {
    bool aborted = false;
    const char* abort_reason = "";
    std::vector<GroundedHit> hits;
};

struct SlotRead {
    bool present = false;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct GroundedObservation {
    std::uint64_t address = 0;
    double offset_x = 0.0;
    double offset_y = 0.0;
    double offset_z = 0.0;
    bool has_offset = false;
    SlotRead slots[static_cast<std::size_t>(GroundedSlot::Count)]{};
    GroundedVec3 oracle_a{};
    GroundedVec3 oracle_c{};
    GroundedVec3 oracle_e{};
    GroundedVec3 oracle_h{};
    bool marker_allocation = false;
    bool float64_only = false;
    std::uint32_t initial_page_matches = 1;
    std::uint32_t qualified_page_matches = 1;
};

struct GroundedClassification {
    GroundedEvidence evidence = GroundedEvidence::Unclassified;
    const char* reason = "";
};

struct OracleSample {
    std::uint32_t sample_id = 0;
    std::string phase;
    std::uint64_t observed_ms = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct LiveAttachRefusal {
    bool blocked = true;
    const char* message = "";
};

struct GroundedQualifyRequest {
    void (*prepare)(void* context, std::uint32_t slot) = nullptr;
    void* context = nullptr;
    GroundedView view{};
    GroundedVec3 oracle_a{};
    GroundedVec3 oracle_c{};
    GroundedVec3 oracle_e{};
    GroundedVec3 oracle_h{};
    ScanClock clock{};
    bool in_vehicle = false;
};

struct ClassifiedAddress {
    std::uint64_t address = 0;
    GroundedEvidence evidence = GroundedEvidence::Unclassified;
    const char* reason = "";
    bool float64_only = false;
};

struct GroundedQualification {
    bool aborted = false;
    const char* abort_reason = "";
    const char* published_live_claim = kGroundedLiveClaimWithheld;
    std::vector<ClassifiedAddress> addresses;
};

class OracleLogWatcher {
public:
    void start_after_existing(std::uint64_t existing_size);
    void poll(std::string_view contents, std::uint64_t observed_ms);
    void poll_observed(std::string_view contents);
    // Read-only tail. A failed open leaves the watcher unchanged.
    bool poll_file(const std::wstring& path, std::uint64_t observed_ms);

    std::uint32_t accepted() const noexcept { return accepted_; }
    std::uint32_t rejected() const noexcept { return rejected_; }
    std::uint32_t generation() const noexcept { return generation_; }
    const std::vector<OracleSample>& samples() const noexcept { return samples_; }

    std::vector<OracleSample> samples_in_window(std::uint64_t center_ms,
                                                std::uint32_t half_window_ms) const;

private:
    std::uint64_t cursor_ = 0;
    std::uint32_t accepted_ = 0;
    std::uint32_t rejected_ = 0;
    std::uint32_t generation_ = 0;
    bool started_ = false;
    std::string pending_;
    std::vector<OracleSample> samples_;

    void consume_pending(std::uint64_t observed_ms);
};

std::uint64_t grounded_observation_tick() noexcept;

const char* grounded_phase_name(std::uint32_t index) noexcept;
std::uint32_t grounded_phase_count() noexcept;
bool grounded_phase_is_standing(std::string_view phase) noexcept;
bool grounded_phase_duration_ok(std::uint64_t start_ms, std::uint64_t end_ms) noexcept;

bool grounded_region_eligible(const GroundedRegion& region) noexcept;
bool grounded_module_excludes(GroundedModuleKind kind) noexcept;
bool grounded_address_excluded(std::uint64_t address,
                               const GroundedModule* modules,
                               std::size_t module_count) noexcept;
bool grounded_buffer_contains_marker(const std::uint8_t* data, std::size_t size) noexcept;

bool grounded_oracle_invalid(GroundedVec3 value) noexcept;
bool grounded_candidate_component_absurd(double component) noexcept;
bool grounded_exact_match(GroundedVec3 memory, GroundedVec3 oracle) noexcept;
bool grounded_near_match(GroundedVec3 memory, GroundedVec3 oracle) noexcept;
bool grounded_select_offset(GroundedVec3 memory, GroundedVec3 oracle, GroundedVec3& offset) noexcept;
bool grounded_offset_holds(GroundedVec3 memory, GroundedVec3 oracle, GroundedVec3 offset) noexcept;

double grounded_distance(GroundedVec3 a, GroundedVec3 b) noexcept;
bool grounded_is_stale(GroundedVec3 memory,
                       GroundedVec3 current_oracle,
                       const GroundedVec3* older_oracles,
                       std::size_t older_count,
                       GroundedVec3 offset) noexcept;
bool grounded_displacement_agrees(GroundedVec3 oracle_from,
                                  GroundedVec3 oracle_to,
                                  GroundedVec3 memory_from,
                                  GroundedVec3 memory_to) noexcept;
bool grounded_turn_stable(GroundedVec3 before, GroundedVec3 during) noexcept;
bool grounded_double_read_stable(GroundedVec3 first, GroundedVec3 second) noexcept;
bool grounded_standing_positions_separated(GroundedVec3 a,
                                           GroundedVec3 b,
                                           GroundedVec3 c,
                                           GroundedVec3 d) noexcept;

bool grounded_copy_triple(const GroundedView& view,
                          std::uint64_t address,
                          GroundedVec3& out) noexcept;

bool grounded_standing_median(const OracleSample* samples,
                              std::size_t count,
                              GroundedVec3& median) noexcept;
bool grounded_standing_oracle_stable(const OracleSample* samples, std::size_t count) noexcept;

StreamScanResult stream_scan_float32(const StreamScanRequest& request);
RecheckResult recheck_grounded_hits(const RecheckRequest& request);

GroundedClassification classify_grounded_observation(const GroundedObservation& observation) noexcept;
GroundedQualification qualify_grounded_session(GroundedQualifyRequest& request);

bool grounded_live_requirements_met(std::size_t strong_count,
                                    bool cleo_absent_control_passed) noexcept;
const char* grounded_session_claim() noexcept;

LiveAttachRefusal evaluate_grounded_live_attach() noexcept;

bool grounded_process_identity_holds(std::uint32_t expected_pid,
                                     std::uint64_t expected_creation,
                                     std::string_view expected_path,
                                     std::uint32_t live_pid,
                                     std::uint64_t live_creation,
                                     std::string_view live_path) noexcept;

bool grounded_vehicle_aborts_session(bool in_vehicle) noexcept;

const char* to_string(GroundedEvidence evidence) noexcept;

} // namespace deflux::game::gta_sa_de
