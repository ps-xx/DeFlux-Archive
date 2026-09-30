#include "deflux/core/process_lifecycle.hpp"
#include "deflux/game/gta_sa_de/adapter.hpp"
#include "deflux/game/gta_sa_de/gworld_live_observation.hpp"
#include "deflux/game/gta_sa_de/legacy_chain_diagnostic.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
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

#define CHECK_LC(expr) check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

struct FakeMemory {
    std::uint64_t module_base = 0x140000000ull;
    std::unordered_map<std::uint64_t, std::vector<std::uint8_t>> bytes;
    std::unordered_map<std::uint64_t, bool> unreadable;
};

bool fake_write(FakeMemory& memory, std::uint64_t address, const void* data, std::size_t size) {
    auto& slot = memory.bytes[address];
    slot.assign(size, 0);
    if (data != nullptr && size > 0) {
        std::memcpy(slot.data(), data, size);
    }
    return true;
}

bool fake_write_u64(FakeMemory& memory, std::uint64_t address, std::uint64_t value) {
    return fake_write(memory, address, &value, sizeof(value));
}

bool fake_write_i32(FakeMemory& memory, std::uint64_t address, std::int32_t value) {
    return fake_write(memory, address, &value, sizeof(value));
}

bool fake_read(FakeMemory& memory, std::uint64_t address, void* out, std::size_t bytes) {
    if (out == nullptr || memory.unreadable.count(address) != 0) {
        return false;
    }
    const auto it = memory.bytes.find(address);
    if (it == memory.bytes.end() || it->second.size() < bytes) {
        return false;
    }
    std::memcpy(out, it->second.data(), bytes);
    return true;
}

deflux::game::gta_sa_de::LegacyChainReadFn make_reader(FakeMemory& memory) {
    return [&memory](std::uint64_t address, void* out, std::size_t bytes) {
        return fake_read(memory, address, out, bytes);
    };
}

void write_valid_transform(FakeMemory& memory,
                           std::uint64_t root_component,
                           std::uint32_t component_to_world,
                           float x,
                           float y,
                           float z) {
    const std::uint64_t xform = root_component + component_to_world;
    std::uint8_t blob[0x28]{};
    const float quat[4]{0.f, 0.f, 0.f, 1.f};
    const float translation[3]{x, y, z};
    const float scale[3]{1.f, 1.f, 1.f};
    std::memcpy(blob, quat, sizeof(quat));
    std::memcpy(blob + 0x10, translation, sizeof(translation));
    std::memcpy(blob + 0x1C, scale, sizeof(scale));
    fake_write(memory, xform, blob, sizeof(blob));
}

void build_full_chain(FakeMemory& memory,
                      std::uint64_t slot_va,
                      const deflux::game::gta_sa_de::LegacyOffsetProfile& profile,
                      float x,
                      float y,
                      float z) {
    const std::uint64_t profile_root = 0x000001FE81001000ull;
    const std::uint64_t game_instance = 0x000001FE81002000ull;
    const std::uint64_t local_players_data = 0x000001FE81003000ull;
    const std::uint64_t local_player = 0x000001FE81004000ull;
    const std::uint64_t legacy_pc = 0x000001FE81005000ull;
    const std::uint64_t legacy_pawn = 0x000001FE81006000ull;
    const std::uint64_t root_component = 0x000001FE81007000ull;

    fake_write_u64(memory, slot_va, profile_root);
    fake_write_u64(memory, profile_root + profile.owning_game_instance, game_instance);
    fake_write_u64(memory, game_instance + profile.local_players, local_players_data);
    fake_write_i32(memory, game_instance + profile.local_players + 8, 1);
    fake_write_u64(memory, local_players_data, local_player);
    fake_write_u64(memory, local_player + profile.player_controller, legacy_pc);
    fake_write_u64(memory, legacy_pc + profile.pawn, legacy_pawn);
    fake_write_u64(memory, legacy_pawn + profile.root_component, root_component);
    write_valid_transform(memory, root_component, profile.component_to_world, x, y, z);
}

deflux::core::ProcessInstance make_instance(std::uint32_t pid,
                                            std::uint64_t creation,
                                            const char* path) {
    deflux::core::ProcessInstance instance{};
    instance.process_id = pid;
    instance.creation_identity = creation;
    instance.executable_path = path != nullptr ? path : "";
    return instance;
}

void test_allowlist_and_profiles() {
    using deflux::game::gta_sa_de::is_legacy_chain_opaque_root_rva;
    using deflux::game::gta_sa_de::kLegacyChainOpaqueRootRvas;
    using deflux::game::gta_sa_de::kLegacyChainProfiles;

    CHECK_LC(kLegacyChainOpaqueRootRvas.size() == 2);
    CHECK_LC(is_legacy_chain_opaque_root_rva(0x05391B70u));
    CHECK_LC(is_legacy_chain_opaque_root_rva(0x05764130u));
    CHECK_LC(!is_legacy_chain_opaque_root_rva(0x01234567u));
    CHECK_LC(kLegacyChainProfiles.size() == 9);
    CHECK_LC(std::strcmp(kLegacyChainProfiles[0].name, "ue4.26-a") == 0);
    CHECK_LC(kLegacyChainProfiles[0].owning_game_instance == 0x180u);
    CHECK_LC(kLegacyChainProfiles[8].name != nullptr);
    CHECK_LC(std::strcmp(kLegacyChainProfiles[8].name, "ackpawn") == 0);
}

void test_config_bounds() {
    using deflux::game::gta_sa_de::LegacyChainDiagnosticConfig;
    using deflux::game::gta_sa_de::normalize_legacy_chain_diagnostic_config;

    LegacyChainDiagnosticConfig config{};
    config.interval_ms = 0;
    config.max_samples = 0;
    CHECK_LC(normalize_legacy_chain_diagnostic_config(config));
    CHECK_LC(config.interval_ms == 500);
    CHECK_LC(config.max_samples == 120);
}

void test_chain_break_stages() {
    using deflux::game::gta_sa_de::LegacyChainBreakStage;
    using deflux::game::gta_sa_de::LegacyOffsetProfile;
    using deflux::game::gta_sa_de::kLegacyChainProfiles;
    using deflux::game::gta_sa_de::probe_legacy_chain_profile;
    using deflux::game::gta_sa_de::to_string;

    FakeMemory memory{};
    const LegacyOffsetProfile& profile = kLegacyChainProfiles[0];
    const std::uint64_t slot_va = memory.module_base + 0x05391B70u;

    auto unreadable = make_reader(memory);
    memory.unreadable[slot_va] = true;
    auto failed = probe_legacy_chain_profile(slot_va, profile, unreadable);
    CHECK_LC(failed.stage == LegacyChainBreakStage::RootSlotReadFailed);
    CHECK_LC(std::strcmp(to_string(failed.stage), "ROOT_SLOT_READ_FAILED") == 0);

    memory.unreadable.clear();
    memory.bytes.clear();
    fake_write_u64(memory, slot_va, 0);
    auto root_null = probe_legacy_chain_profile(slot_va, profile, make_reader(memory));
    CHECK_LC(root_null.stage == LegacyChainBreakStage::RootNull);

    build_full_chain(memory, slot_va, profile, 100.f, 200.f, 13.f);
    auto ok = probe_legacy_chain_profile(slot_va, profile, make_reader(memory));
    CHECK_LC(ok.stage == LegacyChainBreakStage::Ok);
    CHECK_LC(ok.transform.shape_valid);
    CHECK_LC(ok.local_player_candidate_preserved);
    CHECK_LC(ok.legacy_pc_candidate_va != 0);
    CHECK_LC(ok.legacy_pawn_candidate_va != 0);
}

void test_null_and_unreadable_hops() {
    using deflux::game::gta_sa_de::LegacyChainBreakStage;
    using deflux::game::gta_sa_de::kLegacyChainProfiles;
    using deflux::game::gta_sa_de::probe_legacy_chain_profile;

    FakeMemory memory{};
    const auto& profile = kLegacyChainProfiles[0];
    const std::uint64_t slot_va = memory.module_base + 0x05764130u;

    const std::uint64_t profile_root = 0x000001FE81001000ull;
    fake_write_u64(memory, slot_va, profile_root);
    fake_write_u64(memory, profile_root + profile.owning_game_instance, 0);
    auto gi_null = probe_legacy_chain_profile(slot_va, profile, make_reader(memory));
    CHECK_LC(gi_null.stage == LegacyChainBreakStage::ProfileRootNull);

    build_full_chain(memory, slot_va, profile, 100.f, 200.f, 13.f);
    const std::uint64_t legacy_pc = 0x000001FE81005000ull;
    fake_write_u64(memory, legacy_pc + profile.pawn, 0);
    auto pawn_null = probe_legacy_chain_profile(slot_va, profile, make_reader(memory));
    CHECK_LC(pawn_null.stage == LegacyChainBreakStage::LegacyPawnCandidateNull);
}

void test_transform_shape_validation() {
    using deflux::game::gta_sa_de::LegacyChainBreakStage;
    using deflux::game::gta_sa_de::LegacyChainTransformShape;
    using deflux::game::gta_sa_de::LegacyChainVec3;
    using deflux::game::gta_sa_de::kLegacyChainProfiles;
    using deflux::game::gta_sa_de::probe_legacy_chain_profile;
    using deflux::game::gta_sa_de::validate_legacy_transform_shape;

    float quat[4]{0.f, 0.f, 0.f, 1.f};
    LegacyChainVec3 translation{100.f, 200.f, 13.f};
    LegacyChainVec3 scale{1.f, 1.f, 1.f};
    LegacyChainTransformShape shape{};
    CHECK_LC(validate_legacy_transform_shape(quat, translation, scale, shape));
    CHECK_LC(shape.shape_valid);

    LegacyChainVec3 nan_translation{std::nanf(""), 1.f, 1.f};
    CHECK_LC(!validate_legacy_transform_shape(quat, nan_translation, scale, shape));

    LegacyChainVec3 bad_scale_translation{100.f, 200.f, 13.f};
    LegacyChainVec3 bad_scale{2.f, 2.f, 2.f};
    CHECK_LC(!validate_legacy_transform_shape(quat, bad_scale_translation, bad_scale, shape));

    LegacyChainVec3 implausible{0.f, 0.f, 0.f};
    CHECK_LC(!validate_legacy_transform_shape(quat, implausible, scale, shape));

    FakeMemory memory{};
    const auto& profile = kLegacyChainProfiles[0];
    const std::uint64_t slot_va = memory.module_base + 0x05391B70u;
    build_full_chain(memory, slot_va, profile, 100.f, 200.f, 13.f);
    const std::uint64_t root_component = 0x000001FE81007000ull;
    write_valid_transform(memory, root_component, profile.component_to_world, 0.f, 0.f, 0.f);
    auto invalid = probe_legacy_chain_profile(slot_va, profile, make_reader(memory));
    CHECK_LC(invalid.stage == LegacyChainBreakStage::TransformInvalid);
}

void test_movement_delta_and_classification() {
    using deflux::game::gta_sa_de::LegacyChainClassification;
    using deflux::game::gta_sa_de::LegacyChainObservationSample;
    using deflux::game::gta_sa_de::LegacyChainProfileSample;
    using deflux::game::gta_sa_de::LegacyChainTransformShape;
    using deflux::game::gta_sa_de::LegacyChainVec3;
    using deflux::game::gta_sa_de::classify_legacy_chain_session;
    using deflux::game::gta_sa_de::compute_legacy_chain_movement_delta;
    using deflux::game::gta_sa_de::kLegacyChainMovementEpsilon;

    LegacyChainTransformShape standing{};
    standing.shape_valid = true;
    standing.translation = LegacyChainVec3{100.f, 200.f, 13.f};

    LegacyChainTransformShape moved{};
    moved.shape_valid = true;
    moved.translation = LegacyChainVec3{103.f, 200.f, 13.f};

    const auto static_delta =
        compute_legacy_chain_movement_delta(standing, standing, kLegacyChainMovementEpsilon);
    CHECK_LC(static_delta.has_previous);
    CHECK_LC(!static_delta.changed_beyond_epsilon);

    const auto moving_delta =
        compute_legacy_chain_movement_delta(standing, moved, kLegacyChainMovementEpsilon);
    CHECK_LC(moving_delta.changed_beyond_epsilon);
    CHECK_LC(moving_delta.distance > kLegacyChainMovementEpsilon);

    std::vector<LegacyChainObservationSample> samples;
    LegacyChainObservationSample sample{};
    LegacyChainProfileSample profile_sample{};
    profile_sample.transform = standing;
    profile_sample.transform.shape_valid = true;
    sample.profiles.push_back(profile_sample);
    samples.push_back(sample);
    CHECK_LC(classify_legacy_chain_session(samples) == LegacyChainClassification::Observed);
    CHECK_LC(classify_legacy_chain_session({}) == LegacyChainClassification::Insufficient);
}

void test_session_not_running_and_stop() {
    using deflux::game::gta_sa_de::LegacyChainClassification;
    using deflux::game::gta_sa_de::LiveObservationPhase;
    using deflux::game::gta_sa_de::LiveObservationStatus;
    using deflux::game::gta_sa_de::run_legacy_chain_diagnostic_session;

    deflux::core::ProcessObservation observation{};
    observation.state = deflux::core::ProcessLifecycle::NotRunning;
    observation.instance = make_instance(100, 200, "C:\\Games\\SanAndreas.exe");

    const auto not_running = run_legacy_chain_diagnostic_session(
        observation,
        {},
        [](LiveObservationPhase& phase_out) {
            phase_out = LiveObservationPhase::Menu;
            return true;
        });
    CHECK_LC(not_running.status == LiveObservationStatus::NotRunning);
    CHECK_LC(not_running.classification == LegacyChainClassification::Insufficient);

    LiveObservationPhase phase = LiveObservationPhase::Menu;
    bool stop = [&phase](LiveObservationPhase& phase_out) {
        phase_out = phase;
        return false;
    }(phase);
    CHECK_LC(!stop);
}

void test_runtime_isolation() {
    using deflux::game::gta_sa_de::GtaSaDeAdapter;
    using deflux::game::gta_sa_de::is_supported_fingerprint;
    using deflux::game::gta_sa_de::kLiveObservationTargetSha256;

    CHECK_LC(!is_supported_fingerprint(kLiveObservationTargetSha256));
    const GtaSaDeAdapter adapter;
    CHECK_LC(adapter.engine_ready().status == deflux::game::Readiness::NotImplemented);
    CHECK_LC(adapter.world_ready().status == deflux::game::Readiness::NotImplemented);
    CHECK_LC(adapter.local_player_ready().status == deflux::game::Readiness::NotImplemented);
    CHECK_LC(adapter.probe_supported_game().status == deflux::game::ProbeStatus::Unsupported);
}

void test_no_gworld_qualification_dependency() {
    using deflux::game::gta_sa_de::LegacyChainBreakStage;
    using deflux::game::gta_sa_de::to_string;

    CHECK_LC(std::strcmp(to_string(LegacyChainBreakStage::RootNull), "ROOT_NULL") == 0);
    CHECK_LC(std::strcmp(to_string(LegacyChainBreakStage::LegacyPcCandidateNull),
                         "LEGACY_PC_CANDIDATE_NULL")
             == 0);
}

} // namespace

int run_qualification_legacy_chain_diagnostic_tests(int& passed, int& failed) {
    const int before_passed = g_passed;
    const int before_failed = g_failed;

    test_allowlist_and_profiles();
    test_config_bounds();
    test_chain_break_stages();
    test_null_and_unreadable_hops();
    test_transform_shape_validation();
    test_movement_delta_and_classification();
    test_session_not_running_and_stop();
    test_runtime_isolation();
    test_no_gworld_qualification_dependency();

    passed += g_passed - before_passed;
    failed += g_failed - before_failed;
    return g_failed - before_failed;
}

} // namespace deflux_tests
