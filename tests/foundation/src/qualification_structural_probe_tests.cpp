#include "deflux/core/process_lifecycle.hpp"
#include "deflux/game/gta_sa_de/adapter.hpp"
#include "deflux/game/gta_sa_de/detection.hpp"
#include "deflux/game/gta_sa_de/gworld_live_observation.hpp"

#include <array>
#include <cstdio>
#include <cstring>
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

#define CHECK_SP(expr) check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

struct FakeMemory {
    std::uint64_t module_base = 0x140000000ull;
    std::uint64_t module_size = 0x6000000ull;
    std::unordered_map<std::uint64_t, std::uint64_t> qwords;
    std::unordered_map<std::uint64_t, std::vector<std::uint8_t>> blobs;
    std::unordered_map<std::uint64_t, bool> unreadable;
    std::vector<deflux::game::gta_sa_de::LiveObservationModuleRange> modules;
};

bool fake_read(FakeMemory& memory, std::uint64_t address, void* out, std::size_t bytes) {
    if (out == nullptr || bytes == 0) {
        return false;
    }
    if (memory.unreadable.count(address) != 0) {
        return false;
    }

    for (const auto& entry : memory.blobs) {
        const std::uint64_t base = entry.first;
        const std::vector<std::uint8_t>& blob = entry.second;
        if (address >= base && address + bytes <= base + blob.size()) {
            std::memcpy(out, blob.data() + (address - base), bytes);
            return true;
        }
    }

    if (bytes != 8) {
        return false;
    }
    const auto it = memory.qwords.find(address);
    if (it == memory.qwords.end()) {
        return false;
    }
    std::memcpy(out, &it->second, sizeof(it->second));
    return true;
}

void write_object_prefix(FakeMemory& memory,
                         std::uint64_t object_ptr,
                         const std::vector<std::uint64_t>& pointer_fields) {
    std::vector<std::uint8_t> blob(deflux::game::gta_sa_de::kStructuralObjectPrefixBytes, 0);
    for (std::size_t i = 0; i < pointer_fields.size()
                            && (i * 8) < blob.size(); ++i) {
        const std::uint64_t value = pointer_fields[i];
        std::memcpy(blob.data() + (i * 8), &value, sizeof(value));
    }
    memory.blobs[object_ptr] = std::move(blob);
}

deflux::game::gta_sa_de::LiveObservationReadFn make_read_fn(FakeMemory& memory) {
    return [&memory](std::uint64_t address, void* out, std::size_t bytes) {
        return fake_read(memory, address, out, bytes);
    };
}

deflux::game::gta_sa_de::LiveObservationProbeContext make_context(FakeMemory& memory) {
    deflux::game::gta_sa_de::LiveObservationProbeContext context{};
    context.module_base = memory.module_base;
    context.module_size = memory.module_size;
    context.module_ranges = memory.modules;
    return context;
}

void test_structural_allowlist() {
    using deflux::game::gta_sa_de::is_structural_identity_probe_candidate;
    using deflux::game::gta_sa_de::kStructuralIdentityProbeCandidateRva;
    using deflux::game::gta_sa_de::probe_candidate_structure;

    CHECK_SP(kStructuralIdentityProbeCandidateRva == 0x05764130u);
    CHECK_SP(is_structural_identity_probe_candidate(0x05764130u));
    CHECK_SP(!is_structural_identity_probe_candidate(0x05391B70u));

    FakeMemory memory{};
    memory.modules.push_back({memory.module_base, memory.module_size});
    const auto rejected = probe_candidate_structure(
        0x05391B70u, make_context(memory), make_read_fn(memory));
    CHECK_SP(rejected.classification
             == deflux::game::gta_sa_de::StructureIdentityClassification::Insufficient);
    CHECK_SP(rejected.fields.empty());
}

void test_null_and_noncanonical() {
    using deflux::game::gta_sa_de::StructureIdentityClassification;
    using deflux::game::gta_sa_de::probe_candidate_structure;

    FakeMemory memory{};
    memory.modules.push_back({memory.module_base, memory.module_size});
    const std::uint64_t slot_va = memory.module_base + 0x05764130u;

    memory.qwords[slot_va] = 0;
    auto null_snap = probe_candidate_structure(0x05764130u, make_context(memory), make_read_fn(memory));
    CHECK_SP(null_snap.object_is_null);
    CHECK_SP(null_snap.classification == StructureIdentityClassification::Insufficient);

    memory.qwords[slot_va] = 0xFFFFull;
    auto bad_snap = probe_candidate_structure(0x05764130u, make_context(memory), make_read_fn(memory));
    CHECK_SP(bad_snap.classification == StructureIdentityClassification::Insufficient
             || bad_snap.classification == StructureIdentityClassification::Contradicted);
}

void test_observed_structure_and_field_limits() {
    using deflux::game::gta_sa_de::StructureIdentityClassification;
    using deflux::game::gta_sa_de::kStructuralMaxPointerFields;
    using deflux::game::gta_sa_de::kStructuralObjectPrefixBytes;
    using deflux::game::gta_sa_de::probe_candidate_structure;

    FakeMemory memory{};
    memory.modules.push_back({memory.module_base, memory.module_size});
    const std::uint64_t slot_va = memory.module_base + 0x05764130u;
    const std::uint64_t object_ptr = 0x000002A000001000ull;
    const std::uint64_t vtable_ptr = memory.module_base + 0x2000ull;
    const std::uint64_t field_ptr = memory.module_base + 0x3000ull;

    memory.qwords[slot_va] = object_ptr;
    memory.qwords[field_ptr] = 0x1111111111111111ull;
    write_object_prefix(memory, object_ptr, {vtable_ptr, field_ptr, 0, 0});

    const auto snap = probe_candidate_structure(0x05764130u, make_context(memory), make_read_fn(memory));
    CHECK_SP(snap.object_prefix_read_ok);
    CHECK_SP(snap.object_prefix_bytes_read == kStructuralObjectPrefixBytes);
    CHECK_SP(snap.fields.size() == kStructuralMaxPointerFields);
    CHECK_SP(snap.first_qword == vtable_ptr);
    CHECK_SP(snap.first_qword_in_module);
    CHECK_SP(snap.fields[0].dereference_attempted);
    CHECK_SP(!snap.fields[0].target_readable);
    CHECK_SP(snap.fields[1].dereference_attempted);
    CHECK_SP(snap.fields[1].target_readable);
    CHECK_SP(snap.fields[2].dereference_attempted == false);
    CHECK_SP(snap.classification == StructureIdentityClassification::Observed);
}

void test_unreadable_prefix_and_field_target() {
    using deflux::game::gta_sa_de::StructureIdentityClassification;
    using deflux::game::gta_sa_de::probe_candidate_structure;

    FakeMemory memory{};
    memory.modules.push_back({memory.module_base, memory.module_size});
    const std::uint64_t slot_va = memory.module_base + 0x05764130u;
    const std::uint64_t object_ptr = 0x000002A000002000ull;
    memory.qwords[slot_va] = object_ptr;
    memory.unreadable[object_ptr] = true;

    const auto unreadable = probe_candidate_structure(0x05764130u, make_context(memory), make_read_fn(memory));
    CHECK_SP(unreadable.classification == StructureIdentityClassification::Insufficient);

    memory.unreadable.clear();
    const std::uint64_t vtable_ptr = memory.module_base + 0x4000ull;
    const std::uint64_t unreadable_ptr = 0x000002A000003000ull;
    write_object_prefix(memory, object_ptr, {vtable_ptr, unreadable_ptr});
    memory.unreadable[unreadable_ptr] = true;

    const auto field_unreadable =
        probe_candidate_structure(0x05764130u, make_context(memory), make_read_fn(memory));
    CHECK_SP(field_unreadable.fields[1].dereference_attempted);
    CHECK_SP(!field_unreadable.fields[1].target_readable);
}

void test_object_inside_module_contradiction() {
    using deflux::game::gta_sa_de::StructureIdentityClassification;
    using deflux::game::gta_sa_de::probe_candidate_structure;

    FakeMemory memory{};
    memory.modules.push_back({memory.module_base, memory.module_size});
    const std::uint64_t slot_va = memory.module_base + 0x05764130u;
    const std::uint64_t object_ptr = memory.module_base + 0x5000ull;
    memory.qwords[slot_va] = object_ptr;

    const auto snap = probe_candidate_structure(0x05764130u, make_context(memory), make_read_fn(memory));
    CHECK_SP(snap.classification == StructureIdentityClassification::Contradicted);
    CHECK_SP(snap.fields.empty());
}

void test_no_recursive_traversal_guard() {
    using deflux::game::gta_sa_de::probe_candidate_structure;

    FakeMemory memory{};
    memory.modules.push_back({memory.module_base, memory.module_size});
    const std::uint64_t slot_va = memory.module_base + 0x05764130u;
    const std::uint64_t object_ptr = 0x000002A000004000ull;
    const std::uint64_t level1 = 0x000002A000005000ull;
    const std::uint64_t level2 = 0x000002A000006000ull;

    memory.qwords[slot_va] = object_ptr;
    memory.qwords[level1] = level2;
    write_object_prefix(memory, object_ptr, {memory.module_base + 0x6000ull, level1});

    const auto snap = probe_candidate_structure(0x05764130u, make_context(memory), make_read_fn(memory));
    CHECK_SP(snap.fields[1].dereference_attempted);
    CHECK_SP(snap.fields[1].target_readable);
    CHECK_SP(snap.fields[1].dereferenced_value == level2);
    CHECK_SP(memory.qwords.count(level2) == 0 || snap.fields.size() <= 32);
}

void test_field_report_includes_all_captured_fields() {
    using deflux::game::gta_sa_de::StructuralIdentityFieldProbe;
    using deflux::game::gta_sa_de::StructuralIdentitySnapshot;
    using deflux::game::gta_sa_de::format_structural_identity_field_lines;
    using deflux::game::gta_sa_de::kStructuralMaxPointerFields;
    using deflux::game::gta_sa_de::probe_candidate_structure;

    FakeMemory memory{};
    memory.modules.push_back({memory.module_base, memory.module_size});
    const std::uint64_t slot_va = memory.module_base + 0x05764130u;
    const std::uint64_t object_ptr = 0x000002A000007000ull;
    const std::uint64_t vtable_ptr = memory.module_base + 0x7000ull;
    memory.qwords[slot_va] = object_ptr;
    write_object_prefix(memory, object_ptr, {vtable_ptr});

    const auto snap = probe_candidate_structure(0x05764130u, make_context(memory), make_read_fn(memory));
    CHECK_SP(snap.fields.size() == kStructuralMaxPointerFields);

    const auto lines = format_structural_identity_field_lines(snap);
    CHECK_SP(lines.size() == kStructuralMaxPointerFields);
    CHECK_SP(snap.fields.back().offset == 0xF8u);
    CHECK_SP(lines.back().find("off=0xF8") != std::string::npos);

    StructuralIdentitySnapshot synthetic{};
    for (std::size_t index = 0; index < kStructuralMaxPointerFields; ++index) {
        StructuralIdentityFieldProbe field{};
        field.offset = static_cast<std::uint32_t>(index * 8);
        synthetic.fields.push_back(field);
    }
    const auto synthetic_lines = format_structural_identity_field_lines(synthetic);
    CHECK_SP(synthetic_lines.size() == 32);
    CHECK_SP(synthetic_lines.front().find("off=0x0") != std::string::npos);
    CHECK_SP(synthetic_lines.back().find("off=0xF8") != std::string::npos);
}

void test_classification_and_isolation() {
    using deflux::game::gta_sa_de::GtaSaDeAdapter;
    using deflux::game::gta_sa_de::LiveObservationStatus;
    using deflux::game::gta_sa_de::StructureIdentityClassification;
    using deflux::game::gta_sa_de::StructuralIdentitySnapshot;
    using deflux::game::gta_sa_de::classify_structure_identity;
    using deflux::game::gta_sa_de::is_supported_fingerprint;
    using deflux::game::gta_sa_de::kLiveObservationTargetSha256;
    using deflux::game::gta_sa_de::run_structural_identity_probe;
    using deflux::game::gta_sa_de::to_string;

    StructuralIdentitySnapshot observed{};
    observed.candidate_rva = 0x05764130u;
    observed.slot_read_ok = true;
    observed.object_is_null = false;
    observed.object_prefix_read_ok = true;
    observed.object_prefix_bytes_read = 256;
    observed.first_qword = 0x140001000ull;
    observed.first_qword_in_module = true;
    CHECK_SP(classify_structure_identity(observed) == StructureIdentityClassification::Observed);
    CHECK_SP(std::strcmp(to_string(StructureIdentityClassification::Observed), "STRUCTURE_OBSERVED")
             == 0);

    deflux::core::ProcessObservation observation{};
    observation.state = deflux::core::ProcessLifecycle::NotRunning;
    const auto session = run_structural_identity_probe(observation);
    CHECK_SP(session.status == LiveObservationStatus::NotRunning);

    CHECK_SP(!is_supported_fingerprint(kLiveObservationTargetSha256));
    const GtaSaDeAdapter adapter;
    CHECK_SP(adapter.engine_ready().status == deflux::game::Readiness::NotImplemented);
}

} // namespace

int run_qualification_structural_probe_tests(int& passed, int& failed) {
    const int before_passed = g_passed;
    const int before_failed = g_failed;

    test_structural_allowlist();
    test_null_and_noncanonical();
    test_observed_structure_and_field_limits();
    test_unreadable_prefix_and_field_target();
    test_object_inside_module_contradiction();
    test_no_recursive_traversal_guard();
    test_field_report_includes_all_captured_fields();
    test_classification_and_isolation();

    passed += g_passed - before_passed;
    failed += g_failed - before_failed;
    return g_failed - before_failed;
}

} // namespace deflux_tests
