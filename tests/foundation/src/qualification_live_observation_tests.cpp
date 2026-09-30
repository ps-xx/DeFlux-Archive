#include "deflux/core/process_lifecycle.hpp"
#include "deflux/game/gta_sa_de/adapter.hpp"
#include "deflux/game/gta_sa_de/detection.hpp"
#include "deflux/game/gta_sa_de/gworld_live_observation.hpp"

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

#define CHECK_LO(expr) check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

struct FakeMemory {
    std::uint64_t module_base = 0x140000000ull;
    std::uint64_t module_size = 0x6000000ull;
    std::unordered_map<std::uint64_t, std::uint64_t> qwords;
    std::unordered_map<std::uint64_t, bool> unreadable;
    std::vector<deflux::game::gta_sa_de::LiveObservationModuleRange> modules;
};

bool fake_read(FakeMemory& memory, std::uint64_t address, void* out, std::size_t bytes) {
    if (out == nullptr || bytes != 8) {
        return false;
    }
    if (memory.unreadable.count(address) != 0) {
        return false;
    }
    const auto it = memory.qwords.find(address);
    if (it == memory.qwords.end()) {
        return false;
    }
    std::memcpy(out, &it->second, sizeof(it->second));
    return true;
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

void test_allowlist_and_target_sha() {
    using deflux::game::gta_sa_de::is_architect_approved_candidate_rva;
    using deflux::game::gta_sa_de::kArchitectApprovedCandidateRvas;
    using deflux::game::gta_sa_de::kLiveObservationTargetSha256;
    using deflux::game::gta_sa_de::sha256_matches_live_observation_target;

    CHECK_LO(kArchitectApprovedCandidateRvas.size() == 2);
    CHECK_LO(is_architect_approved_candidate_rva(0x05391B70u));
    CHECK_LO(is_architect_approved_candidate_rva(0x05764130u));
    CHECK_LO(!is_architect_approved_candidate_rva(0x01234567u));
    CHECK_LO(sha256_matches_live_observation_target(kLiveObservationTargetSha256));
    CHECK_LO(!sha256_matches_live_observation_target(
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
}

void test_phase_labels_and_config() {
    using deflux::game::gta_sa_de::LiveObservationConfig;
    using deflux::game::gta_sa_de::LiveObservationPhase;
    using deflux::game::gta_sa_de::normalize_live_observation_config;
    using deflux::game::gta_sa_de::parse_live_observation_phase;
    using deflux::game::gta_sa_de::to_string;

    CHECK_LO(parse_live_observation_phase("menu") == LiveObservationPhase::Menu);
    CHECK_LO(parse_live_observation_phase("GAMEPLAY_MOVING") == LiveObservationPhase::GameplayMoving);
    CHECK_LO(parse_live_observation_phase("pause_menu") == LiveObservationPhase::PauseMenu);
    CHECK_LO(parse_live_observation_phase("invalid") == LiveObservationPhase::Unknown);
    CHECK_LO(std::strcmp(to_string(LiveObservationPhase::Loading), "LOADING") == 0);

    LiveObservationConfig config{};
    config.interval_ms = 0;
    config.max_samples = 0;
    CHECK_LO(normalize_live_observation_config(config));
    CHECK_LO(config.interval_ms == 500);
    CHECK_LO(config.max_samples == 60);
}

void test_probe_null_and_non_null() {
    using deflux::game::gta_sa_de::LiveObservationPhase;
    using deflux::game::gta_sa_de::LiveObservationProbeContext;
    using deflux::game::gta_sa_de::LiveObservationReadFn;
    using deflux::game::gta_sa_de::probe_candidate_slot;

    FakeMemory memory{};
    memory.modules.push_back({memory.module_base, memory.module_size});
    const std::uint64_t slot_va = memory.module_base + 0x05391B70u;
    memory.qwords[slot_va] = 0;

    LiveObservationProbeContext context{};
    context.module_base = memory.module_base;
    context.module_size = memory.module_size;
    context.module_ranges = memory.modules;

    LiveObservationReadFn read = [&memory](std::uint64_t address, void* out, std::size_t bytes) {
        return fake_read(memory, address, out, bytes);
    };

    const auto null_sample = probe_candidate_slot(
        0x05391B70u, context, read, LiveObservationPhase::Menu, 0, 100);
    CHECK_LO(null_sample.slot_read_ok);
    CHECK_LO(null_sample.slot_is_null);
    CHECK_LO(!null_sample.first_value_readable);

    const std::uint64_t object_ptr = 0x000002A000001000ull;
    const std::uint64_t vtable_ptr = memory.module_base + 0x1000ull;
    memory.qwords[slot_va] = object_ptr;
    memory.qwords[object_ptr] = vtable_ptr;

    const auto object_sample = probe_candidate_slot(
        0x05391B70u, context, read, LiveObservationPhase::GameplayStanding, 1, 200);
    CHECK_LO(object_sample.slot_read_ok);
    CHECK_LO(!object_sample.slot_is_null);
    CHECK_LO(object_sample.slot_is_canonical);
    CHECK_LO(object_sample.first_value_readable);
    CHECK_LO(object_sample.first_pointer_value == vtable_ptr);
    CHECK_LO(object_sample.first_value_in_module);
}

void test_probe_invalid_and_failed_reads() {
    using deflux::game::gta_sa_de::LiveObservationPhase;
    using deflux::game::gta_sa_de::LiveObservationProbeContext;
    using deflux::game::gta_sa_de::probe_candidate_slot;

    FakeMemory memory{};
    memory.modules.push_back({memory.module_base, memory.module_size});
    const std::uint64_t slot_va = memory.module_base + 0x05764130u;
    memory.qwords[slot_va] = 0xFFFFull;
    memory.unreadable[slot_va] = true;

    LiveObservationProbeContext context{};
    context.module_base = memory.module_base;
    context.module_size = memory.module_size;
    context.module_ranges = memory.modules;

    const auto failed = probe_candidate_slot(
        0x05764130u,
        context,
        [&memory](std::uint64_t address, void* out, std::size_t bytes) {
            return fake_read(memory, address, out, bytes);
        },
        LiveObservationPhase::Menu,
        0,
        1);
    CHECK_LO(!failed.slot_read_ok);

    memory.unreadable.clear();
    memory.qwords[slot_va] = 0xFFFFull;
    const auto noncanonical = probe_candidate_slot(
        0x05764130u,
        context,
        [&memory](std::uint64_t address, void* out, std::size_t bytes) {
            return fake_read(memory, address, out, bytes);
        },
        LiveObservationPhase::Menu,
        1,
        2);
    CHECK_LO(noncanonical.slot_read_ok);
    CHECK_LO(!noncanonical.slot_is_canonical);
    CHECK_LO(!noncanonical.first_value_readable);

    const auto unsupported = probe_candidate_slot(
        0x01234567u,
        context,
        [&memory](std::uint64_t address, void* out, std::size_t bytes) {
            return fake_read(memory, address, out, bytes);
        },
        LiveObservationPhase::Menu,
        2,
        3);
    CHECK_LO(!unsupported.slot_read_ok);
}

void test_aggregation_and_consistency() {
    using deflux::game::gta_sa_de::LiveObservationConsistency;
    using deflux::game::gta_sa_de::LiveObservationPhase;
    using deflux::game::gta_sa_de::LiveObservationSample;
    using deflux::game::gta_sa_de::classify_live_observation_consistency;
    using deflux::game::gta_sa_de::summarize_live_observation_candidate;

    CHECK_LO(classify_live_observation_consistency(0, 0, 0)
             == LiveObservationConsistency::InsufficientData);
    CHECK_LO(classify_live_observation_consistency(3, 3, 1)
             == LiveObservationConsistency::Consistent);
    CHECK_LO(classify_live_observation_consistency(3, 3, 2)
             == LiveObservationConsistency::Inconsistent);

    std::vector<LiveObservationSample> samples;
    LiveObservationSample a{};
    a.candidate_rva = 0x05391B70u;
    a.phase = LiveObservationPhase::Menu;
    a.slot_read_ok = true;
    a.slot_value = 0x1000;
    a.first_value_in_module = true;
    samples.push_back(a);

    LiveObservationSample b = a;
    b.phase = LiveObservationPhase::GameplayMoving;
    b.slot_value = 0x1000;
    samples.push_back(b);

    LiveObservationSample c = a;
    c.phase = LiveObservationPhase::GameplayMoving;
    c.slot_value = 0x2000;
    samples.push_back(c);

    const auto summary = summarize_live_observation_candidate(0x05391B70u, samples);
    CHECK_LO(summary.samples == 3);
    CHECK_LO(summary.read_successes == 3);
    CHECK_LO(summary.distinct_pointer_values == 2);
    CHECK_LO(summary.consistency == LiveObservationConsistency::Inconsistent);
    CHECK_LO(summary.by_phase.size() == 2);
}

void test_process_identity_and_session_stop() {
    using deflux::game::gta_sa_de::LiveObservationPhase;
    using deflux::game::gta_sa_de::LiveObservationStatus;
    using deflux::game::gta_sa_de::process_identity_matches;
    using deflux::game::gta_sa_de::run_live_candidate_observation_session;

    const auto left = make_instance(100, 200, "C:\\Games\\SanAndreas.exe");
    const auto same = make_instance(100, 200, "C:\\Games\\SanAndreas.exe");
    const auto changed = make_instance(100, 201, "C:\\Games\\SanAndreas.exe");
    CHECK_LO(process_identity_matches(left, same));
    CHECK_LO(!process_identity_matches(left, changed));

    deflux::core::ProcessObservation observation{};
    observation.state = deflux::core::ProcessLifecycle::NotRunning;
    observation.instance = left;

    std::size_t phase_calls = 0;
    const auto result = run_live_candidate_observation_session(
        observation,
        {},
        [&phase_calls](LiveObservationPhase& phase_out) {
            ++phase_calls;
            phase_out = LiveObservationPhase::Menu;
            return phase_calls < 3;
        });
    CHECK_LO(result.status == LiveObservationStatus::NotRunning);
    CHECK_LO(phase_calls == 0);
}

void test_runtime_isolation_and_registry() {
    using deflux::game::gta_sa_de::GtaSaDeAdapter;
    using deflux::game::gta_sa_de::is_supported_fingerprint;
    using deflux::game::gta_sa_de::kLiveObservationTargetSha256;

    CHECK_LO(!is_supported_fingerprint(kLiveObservationTargetSha256));

    const GtaSaDeAdapter adapter;
    CHECK_LO(adapter.engine_ready().status == deflux::game::Readiness::NotImplemented);
    CHECK_LO(adapter.world_ready().status == deflux::game::Readiness::NotImplemented);
    CHECK_LO(adapter.probe_supported_game().status == deflux::game::ProbeStatus::Unsupported);
}

} // namespace

int run_qualification_live_observation_tests(int& passed, int& failed) {
    const int before_passed = g_passed;
    const int before_failed = g_failed;

    test_allowlist_and_target_sha();
    test_phase_labels_and_config();
    test_probe_null_and_non_null();
    test_probe_invalid_and_failed_reads();
    test_aggregation_and_consistency();
    test_process_identity_and_session_stop();
    test_runtime_isolation_and_registry();

    passed += g_passed - before_passed;
    failed += g_failed - before_failed;
    return g_failed - before_failed;
}

} // namespace deflux_tests
