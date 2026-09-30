#include "deflux/game/gta_sa_de/behavioral_transform_research.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace deflux_tests {
namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) {
        ++g_passed;
        return;
    }
    ++g_failed;
    std::printf("FAIL %s:%d: %s\n", __FILE__, line, expression);
}

#define CHECK_BT(condition) check(static_cast<bool>(condition), #condition, __LINE__)

using deflux::game::gta_sa_de::BehaviorSignature;
using deflux::game::gta_sa_de::BehavioralProtocol;
using deflux::game::gta_sa_de::CandidateClass;
using deflux::game::gta_sa_de::CrossSessionCorrelation;
using deflux::game::gta_sa_de::PhaseMarker;
using deflux::game::gta_sa_de::PhaseObservation;
using deflux::game::gta_sa_de::RegionDescriptor;
using deflux::game::gta_sa_de::ScanBudget;
using deflux::game::gta_sa_de::TransformCandidate;
using deflux::game::gta_sa_de::Vec3;
using deflux::game::gta_sa_de::behavior_signature;
using deflux::game::gta_sa_de::behavioral_phase_in_order;
using deflux::game::gta_sa_de::classify_research_region;
using deflux::game::gta_sa_de::candidate_observations_ready;
using deflux::game::gta_sa_de::classify_transform_candidate;
using deflux::game::gta_sa_de::consume_sample_bytes;
using deflux::game::gta_sa_de::correlate_behavior_across_sessions;
using deflux::game::gta_sa_de::extract_changed_position_triples;
using deflux::game::gta_sa_de::kBehavioralMaxBytesSampled;
using deflux::game::gta_sa_de::kBehavioralMaxCandidates;
using deflux::game::gta_sa_de::kBehavioralMaxRegionsConsidered;
using deflux::game::gta_sa_de::kResearchMemCommit;
using deflux::game::gta_sa_de::kResearchMemImage;
using deflux::game::gta_sa_de::kResearchMemMapped;
using deflux::game::gta_sa_de::kResearchMemPrivate;
using deflux::game::gta_sa_de::kResearchPageGuard;
using deflux::game::gta_sa_de::kResearchPageNoAccess;
using deflux::game::gta_sa_de::kResearchPageReadWrite;
using deflux::game::gta_sa_de::marker_matches_session;
using deflux::game::gta_sa_de::note_research_region;
using deflux::game::gta_sa_de::observe_candidate_phase;
using deflux::game::gta_sa_de::plausible_position;

RegionDescriptor region(std::uint32_t state, std::uint32_t protect, std::uint32_t type) {
    RegionDescriptor value{};
    value.base = 0x10000;
    value.size = 4096;
    value.state = state;
    value.protect = protect;
    value.type = type;
    return value;
}

void add_phase(TransformCandidate& candidate,
               const char* phase,
               std::uint32_t sequence,
               Vec3 value) {
    PhaseMarker marker{};
    marker.session_nonce = 0x11A;
    marker.sequence = sequence;
    marker.timestamp_ms = 1000ull * sequence;
    marker.protocol = BehavioralProtocol::Primary;
    marker.phase = phase;
    CHECK_BT(observe_candidate_phase(candidate, marker, true, value));
}

TransformCandidate moving_player() {
    TransformCandidate candidate{};
    candidate.address = 0x2000;
    candidate.region_base = 0x1000;
    add_phase(candidate, "BASELINE_STANDING", 1, {0.0f, 0.0f, 0.0f});
    add_phase(candidate, "MOVE_FORWARD", 2, {10.0f, 0.0f, 0.0f});
    add_phase(candidate, "STANDING_AFTER_FORWARD", 3, {10.1f, 0.0f, 0.0f});
    add_phase(candidate, "MOVE_BACKWARD", 4, {4.0f, 0.0f, 0.0f});
    add_phase(candidate, "STANDING_AFTER_BACKWARD", 5, {4.1f, 0.0f, 0.0f});
    add_phase(candidate, "TURN_LEFT", 6, {4.2f, 0.0f, 0.0f});
    add_phase(candidate, "STANDING_AFTER_TURN", 7, {4.2f, 0.1f, 0.0f});
    add_phase(candidate, "RUN_FORWARD", 8, {30.0f, 0.0f, 0.0f});
    add_phase(candidate, "FINAL_STANDING", 9, {30.1f, 0.0f, 0.0f});
    return candidate;
}

void test_region_policy_and_limits() {
    CHECK_BT(classify_research_region(
                 region(kResearchMemCommit, kResearchPageReadWrite, kResearchMemPrivate))
                 .accept);
    CHECK_BT(std::strcmp(classify_research_region(
                             region(kResearchMemCommit, kResearchPageReadWrite, kResearchMemImage))
                             .reason,
                         "image_static")
             == 0);
    CHECK_BT(std::strcmp(classify_research_region(
                             region(kResearchMemCommit, kResearchPageReadWrite, kResearchMemMapped))
                             .reason,
                         "mapped_file")
             == 0);
    CHECK_BT(!classify_research_region(
                  region(kResearchMemCommit, kResearchPageReadWrite | kResearchPageGuard, kResearchMemPrivate))
                  .accept);
    CHECK_BT(!classify_research_region(
                  region(kResearchMemCommit, kResearchPageNoAccess, kResearchMemPrivate))
                  .accept);

    ScanBudget budget{};
    RegionDescriptor accepted = region(kResearchMemCommit, kResearchPageReadWrite, kResearchMemPrivate);
    CHECK_BT(note_research_region(budget, accepted));
    CHECK_BT(budget.regions_accepted == 1);
    CHECK_BT(!consume_sample_bytes(budget, kBehavioralMaxBytesSampled + 1));
    CHECK_BT(budget.aborted);

    ScanBudget regions{};
    RegionDescriptor skipped = region(0, kResearchPageReadWrite, kResearchMemPrivate);
    bool within_limit = true;
    for (std::uint32_t i = 0; i < kBehavioralMaxRegionsConsidered; ++i) {
        within_limit = within_limit && note_research_region(regions, skipped);
    }
    CHECK_BT(within_limit);
    CHECK_BT(!note_research_region(regions, accepted));
    CHECK_BT(regions.aborted);
}

void test_movement_filters() {
    TransformCandidate player = moving_player();
    classify_transform_candidate(player);
    CHECK_BT(player.classification == CandidateClass::BehavioralStrong);

    TransformCandidate stationary = moving_player();
    stationary.observations.clear();
    add_phase(stationary, "BASELINE_STANDING", 1, {1.0f, 2.0f, 3.0f});
    add_phase(stationary, "MOVE_FORWARD", 2, {1.0f, 2.0f, 3.0f});
    add_phase(stationary, "STANDING_AFTER_FORWARD", 3, {1.0f, 2.0f, 3.0f});
    add_phase(stationary, "MOVE_BACKWARD", 4, {1.0f, 2.0f, 3.0f});
    add_phase(stationary, "STANDING_AFTER_BACKWARD", 5, {1.0f, 2.0f, 3.0f});
    add_phase(stationary, "TURN_LEFT", 6, {1.0f, 2.0f, 3.0f});
    add_phase(stationary, "STANDING_AFTER_TURN", 7, {1.0f, 2.0f, 3.0f});
    add_phase(stationary, "RUN_FORWARD", 8, {1.0f, 2.0f, 3.0f});
    add_phase(stationary, "FINAL_STANDING", 9, {1.0f, 2.0f, 3.0f});
    classify_transform_candidate(stationary);
    CHECK_BT(stationary.classification == CandidateClass::Rejected);

    TransformCandidate camera = moving_player();
    camera.observations[5].value = {40.0f, 10.0f, 2.0f};
    camera.observations[6].value = {40.2f, 10.0f, 2.0f};
    classify_transform_candidate(camera);
    CHECK_BT(camera.classification == CandidateClass::CameraLike);

    TransformCandidate camera_only = moving_player();
    add_phase(camera_only, "PLAYER_STILL_CAMERA_ROTATE", 10, {80.0f, 0.0f, 5.0f});
    classify_transform_candidate(camera_only);
    CHECK_BT(camera_only.classification == CandidateClass::CameraLike);

    TransformCandidate gone = moving_player();
    gone.observations.pop_back();
    classify_transform_candidate(gone);
    CHECK_BT(gone.classification == CandidateClass::Transient);
}

void test_float_rejection_and_extraction_cap() {
    CHECK_BT(!plausible_position({std::nanf(""), 0.0f, 0.0f}));
    CHECK_BT(!plausible_position({INFINITY, 0.0f, 0.0f}));
    CHECK_BT(!plausible_position({1.0e20f, 0.0f, 0.0f}));

    std::uint8_t before[16]{};
    std::uint8_t after[16]{};
    const Vec3 old_value{0.0f, 0.0f, 0.0f};
    const Vec3 new_value{5.0f, 0.0f, 0.0f};
    const Vec3 bad{std::nanf(""), 0.0f, 0.0f};
    std::memcpy(before, &old_value, sizeof(old_value));
    std::memcpy(after, &bad, sizeof(bad));
    ScanBudget budget{};
    const auto rejected = extract_changed_position_triples(before, after, 12, 0x1000, 0x1000, budget);
    CHECK_BT(rejected.empty());
    CHECK_BT(budget.candidates_before_filter == 1);

    std::memcpy(after, &new_value, sizeof(new_value));
    ScanBudget capped{};
    capped.candidates_after_filter = kBehavioralMaxCandidates;
    const auto overflow = extract_changed_position_triples(before, after, 12, 0x1000, 0x1000, capped);
    CHECK_BT(overflow.empty());
    CHECK_BT(capped.aborted);
}

const TransformCandidate* only_candidate(const std::vector<TransformCandidate>& candidates) {
    if (candidates.size() != 1 || candidates[0].observations.size() < 2) {
        return nullptr;
    }
    return &candidates[0];
}

void write_triple(std::uint8_t* before,
                   std::uint8_t* after,
                   std::size_t offset,
                   Vec3 from,
                   Vec3 to) {
    std::memcpy(before + offset, &from, sizeof(from));
    std::memcpy(after + offset, &to, sizeof(to));
}

void test_overlap_density_and_sparse_cap() {
    using deflux::game::gta_sa_de::kBehavioralMaxClustersPerPage;

    std::uint8_t before[32]{};
    std::uint8_t after[32]{};
    write_triple(before, after, 0, {0.0f, 0.0f, 0.0f}, {8.0f, 0.0f, 0.0f});
    const float adjacent_before = 1.0f;
    const float adjacent_after = 9.0f;
    std::memcpy(before + 12, &adjacent_before, sizeof(adjacent_before));
    std::memcpy(after + 12, &adjacent_after, sizeof(adjacent_after));
    ScanBudget overlapped{};
    const auto clustered = extract_changed_position_triples(before, after, 16, 0x1000, 0x1000, overlapped);
    const TransformCandidate* clustered_one = only_candidate(clustered);
    CHECK_BT(clustered_one != nullptr && clustered_one->address == 0x1000);
    CHECK_BT(overlapped.candidates_before_filter > 1);
    CHECK_BT(!overlapped.aborted);

    constexpr std::size_t kPage = 4096;
    constexpr std::uint32_t kDensePages = 160;
    std::vector<std::uint8_t> dense_before((kDensePages + 1) * kPage, 0);
    std::vector<std::uint8_t> dense_after(dense_before.size(), 0);
    for (std::uint32_t page = 0; page < kDensePages; ++page) {
        for (std::uint32_t slot = 0; slot < 16; ++slot) {
            write_triple(dense_before.data(),
                         dense_after.data(),
                         static_cast<std::size_t>(page) * kPage + static_cast<std::size_t>(slot) * 16,
                         {0.0f, 0.0f, 0.0f},
                         {4.0f, 2.0f, 0.0f});
        }
    }
    const std::size_t player_offset = static_cast<std::size_t>(kDensePages) * kPage;
    write_triple(dense_before.data(),
                 dense_after.data(),
                 player_offset,
                 {0.0f, 0.0f, 0.0f},
                 {12.0f, 0.0f, 0.0f});
    ScanBudget mixed{};
    const auto kept = extract_changed_position_triples(dense_before.data(),
                                                       dense_after.data(),
                                                       dense_before.size(),
                                                       0x8000,
                                                       0x8000,
                                                       mixed);
    const TransformCandidate* kept_one = only_candidate(kept);
    CHECK_BT(kept_one != nullptr);
    CHECK_BT(kept_one != nullptr && kept_one->address == 0x8000 + player_offset - 8);
    CHECK_BT(kept_one != nullptr && kept_one->observations[1].value.x == 0.0f);
    CHECK_BT(kept_one != nullptr && kept_one->observations[1].value.y == 0.0f);
    CHECK_BT(kept_one != nullptr && kept_one->observations[1].value.z == 12.0f);
    CHECK_BT(mixed.dense_pages_rejected == kDensePages);
    CHECK_BT(!mixed.aborted);
    CHECK_BT(mixed.candidates_before_filter > kBehavioralMaxCandidates);

    constexpr std::uint32_t kSlotsPerPage = kBehavioralMaxClustersPerPage;
    constexpr std::uint32_t kSparsePages = (kBehavioralMaxCandidates / kSlotsPerPage) + 1;
    std::vector<std::uint8_t> sparse_before(static_cast<std::size_t>(kSparsePages) * kPage, 0);
    std::vector<std::uint8_t> sparse_after(sparse_before.size(), 0);
    for (std::uint32_t page = 0; page < kSparsePages; ++page) {
        for (std::uint32_t slot = 0; slot < kSlotsPerPage; ++slot) {
            write_triple(sparse_before.data(),
                         sparse_after.data(),
                         static_cast<std::size_t>(page) * kPage + 128u + static_cast<std::size_t>(slot) * 64u,
                         {0.0f, 0.0f, 0.0f},
                         {3.0f, 0.0f, 0.0f});
        }
    }
    ScanBudget sparse{};
    const auto overflow = extract_changed_position_triples(sparse_before.data(),
                                                           sparse_after.data(),
                                                           sparse_before.size(),
                                                           0x100000,
                                                           0x100000,
                                                           sparse);
    CHECK_BT(overflow.empty());
    CHECK_BT(sparse.aborted);
    CHECK_BT(std::strcmp(sparse.abort_reason, "candidate_limit") == 0);
}

void test_dense_page_straddle_does_not_subscript_empty() {
    constexpr std::size_t kPage = 4096;
    std::vector<std::uint8_t> before(kPage * 2, 0);
    std::vector<std::uint8_t> after(before.size(), 0);
    for (std::uint32_t slot = 0; slot < 9; ++slot) {
        write_triple(before.data(),
                     after.data(),
                     128u + static_cast<std::size_t>(slot) * 64u,
                     {0.0f, 0.0f, 0.0f},
                     {4.0f, 2.0f, 0.0f});
    }
    write_triple(before.data(), after.data(), kPage, {0.0f, 0.0f, 0.0f}, {12.0f, 0.0f, 0.0f});

    ScanBudget budget{};
    const auto kept = extract_changed_position_triples(before.data(),
                                                       after.data(),
                                                       before.size(),
                                                       0x8000,
                                                       0x8000,
                                                       budget);
    const TransformCandidate* survivor = only_candidate(kept);
    CHECK_BT(survivor != nullptr);
    CHECK_BT(survivor != nullptr && survivor->address == 0x8000 + kPage - 8);
    CHECK_BT(survivor != nullptr && survivor->observations[1].value.x == 0.0f);
    CHECK_BT(survivor != nullptr && survivor->observations[1].value.y == 0.0f);
    CHECK_BT(survivor != nullptr && survivor->observations[1].value.z == 12.0f);
    CHECK_BT(budget.dense_pages_rejected == 1);
    CHECK_BT(!budget.aborted);
}

void test_phase_order_nonce_and_cross_session() {
    CHECK_BT(behavioral_phase_in_order(BehavioralProtocol::Primary, 1, "BASELINE_STANDING"));
    CHECK_BT(behavioral_phase_in_order(BehavioralProtocol::Primary, 2, "MOVE_FORWARD"));
    CHECK_BT(!behavioral_phase_in_order(BehavioralProtocol::Primary, 2, "BASELINE_STANDING"));
    CHECK_BT(behavioral_phase_in_order(BehavioralProtocol::Validation, 5, "FINAL_STANDING"));
    CHECK_BT(!behavioral_phase_in_order(BehavioralProtocol::Validation, 5, "RUN_FORWARD"));

    PhaseMarker marker{};
    marker.session_nonce = 9;
    marker.sequence = 1;
    marker.phase = "BASELINE_STANDING";
    CHECK_BT(marker_matches_session(marker, 9));
    CHECK_BT(!marker_matches_session(marker, 8));

    TransformCandidate first = moving_player();
    first.address = 0x1111;
    TransformCandidate second = moving_player();
    second.address = 0x2222;
    std::vector<BehaviorSignature> previous{behavior_signature(first)};
    std::vector<BehaviorSignature> current{behavior_signature(second)};
    CHECK_BT(first.address != second.address);
    CHECK_BT(correlate_behavior_across_sessions(previous, current)
             == CrossSessionCorrelation::UniqueBehavioralMatch);
    current.push_back(behavior_signature(second));
    CHECK_BT(correlate_behavior_across_sessions(previous, current) == CrossSessionCorrelation::Ambiguous);
}

void test_phase_interval_and_stage_order() {
    using deflux::game::gta_sa_de::PhaseIntervalStatus;
    using deflux::game::gta_sa_de::PhaseStage;
    using deflux::game::gta_sa_de::kBehavioralMinActionMs;
    using deflux::game::gta_sa_de::kBehavioralMinSettleMs;
    using deflux::game::gta_sa_de::phase_stage_follows;
    using deflux::game::gta_sa_de::validate_phase_interval;

    CHECK_BT(validate_phase_interval("MOVE_FORWARD", 1000, 1000 + kBehavioralMinActionMs - 1)
             == PhaseIntervalStatus::TooShort);
    CHECK_BT(validate_phase_interval("MOVE_BACKWARD", 5000, 5000 + kBehavioralMinActionMs)
             == PhaseIntervalStatus::Accept);
    CHECK_BT(validate_phase_interval("RUN_FORWARD", 20, 10) == PhaseIntervalStatus::EndBeforeStart);
    CHECK_BT(validate_phase_interval("BASELINE_STANDING", 0, kBehavioralMinSettleMs - 1)
             == PhaseIntervalStatus::TooShort);
    CHECK_BT(validate_phase_interval("FINAL_STANDING", 100, 100 + kBehavioralMinSettleMs)
             == PhaseIntervalStatus::Accept);
    CHECK_BT(phase_stage_follows(PhaseStage::ActionStart, PhaseStage::ActionEnd));
    CHECK_BT(phase_stage_follows(PhaseStage::ActionEnd, PhaseStage::Capture));
    CHECK_BT(!phase_stage_follows(PhaseStage::ActionStart, PhaseStage::Capture));
    CHECK_BT(!phase_stage_follows(PhaseStage::Capture, PhaseStage::ActionStart));
    CHECK_BT(!phase_stage_follows(PhaseStage::ActionEnd, PhaseStage::ActionStart));
}

void test_coverage_windows_and_two_state_filter() {
    using deflux::game::gta_sa_de::CoverageSelection;
    using deflux::game::gta_sa_de::CoverageSlice;
    using deflux::game::gta_sa_de::EligibleSpan;
    using deflux::game::gta_sa_de::kBehavioralMaxBytesSampled;
    using deflux::game::gta_sa_de::select_stratified_coverage_window;

    EligibleSpan spans[2] = {{0x10000, 8192}, {0x30000, 8192}};
    CoverageSelection first{};
    CoverageSelection second{};
    std::vector<CoverageSlice> first_slices;
    std::vector<CoverageSlice> second_slices;
    std::vector<CoverageSlice> repeat;
    CHECK_BT(select_stratified_coverage_window(spans, 2, 0, 4096, 1024, first, first_slices));
    CHECK_BT(select_stratified_coverage_window(spans, 2, 0, 4096, 1024, first, repeat));
    CHECK_BT(select_stratified_coverage_window(spans, 2, 1, 4096, 1024, second, second_slices));
    CHECK_BT(first.bytes_selected <= 4096);
    CHECK_BT(second.bytes_selected <= 4096);
    CHECK_BT(first.bytes_selected > 0);
    CHECK_BT(second.bytes_selected > 0);
    CHECK_BT(first.partial);
    CHECK_BT(second.partial);
    CHECK_BT(first.window_count == 4);
    CHECK_BT(first.window_start != second.window_start);
    CHECK_BT(!first.addresses_comparable_across_processes);
    CHECK_BT(first.strategy_version == 2);
    CHECK_BT(repeat.size() == first_slices.size());
    CHECK_BT(!repeat.empty() && repeat[0].base == first_slices[0].base && repeat[0].size == first_slices[0].size);
    CHECK_BT(!select_stratified_coverage_window(spans, 2, 4, 4096, 1024, first, first_slices));

    EligibleSpan one{0x8000, 2048};
    CoverageSelection full{};
    std::vector<CoverageSlice> full_slices;
    CHECK_BT(select_stratified_coverage_window(&one, 1, 0, kBehavioralMaxBytesSampled, 512, full, full_slices));
    CHECK_BT(full.bytes_selected == 2048);
    CHECK_BT(full.bytes_selected <= kBehavioralMaxBytesSampled);
    CHECK_BT(!full.partial);
    CHECK_BT(full.total_eligible_bytes == 2048);

    TransformCandidate latch{};
    add_phase(latch, "BASELINE_STANDING", 1, {1.0f, 1.0f, 1.0f});
    add_phase(latch, "MOVE_FORWARD", 2, {0.0f, 0.0f, 0.0f});
    add_phase(latch, "STANDING_AFTER_FORWARD", 3, {0.0f, 0.0f, 0.0f});
    add_phase(latch, "MOVE_BACKWARD", 4, {1.0f, 1.0f, 1.0f});
    add_phase(latch, "STANDING_AFTER_BACKWARD", 5, {1.0f, 1.0f, 1.0f});
    add_phase(latch, "TURN_LEFT", 6, {1.0f, 1.0f, 1.0f});
    add_phase(latch, "STANDING_AFTER_TURN", 7, {1.0f, 1.0f, 1.0f});
    add_phase(latch, "RUN_FORWARD", 8, {1.0f, 1.0f, 1.0f});
    add_phase(latch, "FINAL_STANDING", 9, {1.0f, 1.0f, 1.0f});
    classify_transform_candidate(latch);
    CHECK_BT(latch.classification == CandidateClass::Rejected);

    TransformCandidate rich = moving_player();
    classify_transform_candidate(rich);
    CHECK_BT(rich.classification == CandidateClass::BehavioralStrong);

    TransformCandidate empty{};
    CHECK_BT(!candidate_observations_ready(empty));
    empty.observations.push_back(PhaseObservation{});
    CHECK_BT(!candidate_observations_ready(empty));
    empty.observations.push_back(PhaseObservation{});
    CHECK_BT(candidate_observations_ready(empty));

    std::uint8_t before[16]{};
    std::uint8_t after[16]{};
    ScanBudget unchanged{};
    const auto none = extract_changed_position_triples(before, after, 12, 0x1000, 0x1000, unchanged);
    CHECK_BT(none.empty());
}

} // namespace

int run_qualification_behavioral_transform_tests(int& passed, int& failed) {
    test_region_policy_and_limits();
    test_movement_filters();
    test_float_rejection_and_extraction_cap();
    test_overlap_density_and_sparse_cap();
    test_dense_page_straddle_does_not_subscript_empty();
    test_phase_order_nonce_and_cross_session();
    test_phase_interval_and_stage_order();
    test_coverage_windows_and_two_state_filter();
    passed += g_passed;
    failed += g_failed;
    return g_failed == 0 ? 0 : 1;
}

} // namespace deflux_tests
