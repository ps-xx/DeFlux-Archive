#include "deflux/core/process_lifecycle.hpp"
#include "deflux/game/adapter.hpp"
#include "deflux/game/gta_sa_de/adapter.hpp"
#include "deflux/game/gta_sa_de/detection.hpp"
#include "deflux/game/gta_sa_de/qualification.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
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

#define CHECK_Q(expr) check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

constexpr const char kVanillaSha[] =
    "63e633bb51c35fe35ca98f503b62e7ddb8403abe4c9d501cfa9162892453846f";
constexpr const char kUnknownSha[] =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

void write_u16(std::vector<std::uint8_t>& buf, std::size_t off, std::uint16_t value) {
    buf[off] = static_cast<std::uint8_t>(value);
    buf[off + 1] = static_cast<std::uint8_t>(value >> 8);
}

void write_u32(std::vector<std::uint8_t>& buf, std::size_t off, std::uint32_t value) {
    buf[off] = static_cast<std::uint8_t>(value);
    buf[off + 1] = static_cast<std::uint8_t>(value >> 8);
    buf[off + 2] = static_cast<std::uint8_t>(value >> 16);
    buf[off + 3] = static_cast<std::uint8_t>(value >> 24);
}

std::vector<std::uint8_t> make_pe32_plus(std::uint32_t size_of_image,
                                         std::uint32_t data_rva,
                                         std::uint32_t data_size,
                                         bool data_executable) {
    std::vector<std::uint8_t> pe(0x400, 0);
    pe[0] = 'M';
    pe[1] = 'Z';
    write_u32(pe, 0x3C, 0x80);
    pe[0x80] = 'P';
    pe[0x81] = 'E';
    write_u16(pe, 0x84, 0x8664);
    write_u16(pe, 0x86, 2);
    write_u16(pe, 0x94, 0xF0);
    write_u16(pe, 0x98, 0x020B);
    write_u32(pe, 0xD0, size_of_image);
    write_u32(pe, 0xD4, 0x200);

    const std::size_t sect0 = 0x188;
    std::memcpy(pe.data() + sect0, ".text\0\0", 6);
    write_u32(pe, sect0 + 8, 0x1000);
    write_u32(pe, sect0 + 12, 0x1000);
    write_u32(pe, sect0 + 16, 0x200);
    write_u32(pe, sect0 + 36, 0x60000020);

    const std::size_t sect1 = sect0 + 40;
    std::memcpy(pe.data() + sect1, ".data\0\0", 6);
    write_u32(pe, sect1 + 8, data_size);
    write_u32(pe, sect1 + 12, data_rva);
    write_u32(pe, sect1 + 16, 0x200);
    write_u32(pe, sect1 + 36, data_executable ? 0x60000020u : 0xC0000040u);
    return pe;
}

deflux::game::gta_sa_de::QualificationChecks passing_checks() {
    using deflux::game::gta_sa_de::AnchorCheck;
    deflux::game::gta_sa_de::QualificationChecks checks{};
    checks.running = true;
    checks.identity_ok = true;
    checks.identity_stable = true;
    checks.fingerprint_ok = true;
    checks.profile_found = true;
    checks.fingerprint_matches_profile = true;
    checks.inspect_ok = true;
    checks.module_ok = true;
    checks.pe_ok = true;
    checks.rva_in_image = true;
    checks.rva_in_data_section = true;
    checks.anchor = AnchorCheck::Ok;
    return checks;
}

void test_status_names() {
    using deflux::game::gta_sa_de::QualificationStatus;
    using deflux::game::gta_sa_de::to_string;
    CHECK_Q(std::strcmp(to_string(QualificationStatus::NotRunning), "NOT_RUNNING") == 0);
    CHECK_Q(std::strcmp(to_string(QualificationStatus::BuildMismatch), "BUILD_MISMATCH") == 0);
    CHECK_Q(std::strcmp(to_string(QualificationStatus::ProcessChanged), "PROCESS_CHANGED") == 0);
    CHECK_Q(std::strcmp(to_string(QualificationStatus::ModuleInvalid), "MODULE_INVALID") == 0);
    CHECK_Q(std::strcmp(to_string(QualificationStatus::ProfileMissing), "PROFILE_MISSING") == 0);
    CHECK_Q(std::strcmp(to_string(QualificationStatus::AnchorNotFound), "ANCHOR_NOT_FOUND") == 0);
    CHECK_Q(std::strcmp(to_string(QualificationStatus::AnchorInvalid), "ANCHOR_INVALID") == 0);
    CHECK_Q(std::strcmp(to_string(QualificationStatus::Qualified), "QUALIFIED") == 0);
    CHECK_Q(std::strcmp(to_string(QualificationStatus::Error), "ERROR") == 0);
}

void test_complete_reads_and_pointers() {
    using deflux::game::gta_sa_de::complete_read_ok;
    using deflux::game::gta_sa_de::is_aligned_pointer;
    using deflux::game::gta_sa_de::is_canonical_user_pointer;
    using deflux::game::gta_sa_de::pointer_in_range;

    CHECK_Q(complete_read_ok(8, 8, true));
    CHECK_Q(!complete_read_ok(8, 4, true));
    CHECK_Q(!complete_read_ok(8, 8, false));
    CHECK_Q(!complete_read_ok(0, 0, true));
    CHECK_Q(!complete_read_ok(8, 0, false));

    CHECK_Q(!is_canonical_user_pointer(0));
    CHECK_Q(!is_canonical_user_pointer(0xFFF));
    CHECK_Q(is_canonical_user_pointer(0x10000));
    CHECK_Q(!is_canonical_user_pointer(0x800000000000ull));
    CHECK_Q(is_aligned_pointer(0x10000));
    CHECK_Q(!is_aligned_pointer(0x10001));
    CHECK_Q(pointer_in_range(0x140001000, 0x140000000, 0x2000));
    CHECK_Q(!pointer_in_range(0x13FFFFFFF, 0x140000000, 0x2000));
}

void test_pe_parser() {
    using deflux::game::gta_sa_de::PeImageInfo;
    using deflux::game::gta_sa_de::PeParseStatus;
    using deflux::game::gta_sa_de::parse_pe64_headers;
    using deflux::game::gta_sa_de::pe_rva_in_non_executable_section;

    PeImageInfo info{};
    CHECK_Q(parse_pe64_headers(nullptr, 16, info) == PeParseStatus::InvalidArgument);

    std::uint8_t tiny[8]{};
    CHECK_Q(parse_pe64_headers(tiny, sizeof(tiny), info) == PeParseStatus::TooSmall);

    std::uint8_t bad_dos[0x40]{};
    CHECK_Q(parse_pe64_headers(bad_dos, sizeof(bad_dos), info) == PeParseStatus::BadDosMagic);

    auto pe = make_pe32_plus(0x20000, 0x10000, 0x1000, false);
    CHECK_Q(parse_pe64_headers(pe.data(), pe.size(), info) == PeParseStatus::Ok);
    CHECK_Q(info.pe32_plus);
    CHECK_Q(info.machine == 0x8664);
    CHECK_Q(info.size_of_image == 0x20000);
    CHECK_Q(pe_rva_in_non_executable_section(pe.data(), pe.size(), 0x10010));
    CHECK_Q(!pe_rva_in_non_executable_section(pe.data(), pe.size(), 0x1010));
    CHECK_Q(!pe_rva_in_non_executable_section(pe.data(), pe.size(), 0x30000));

    auto exec_data = make_pe32_plus(0x20000, 0x10000, 0x1000, true);
    CHECK_Q(!pe_rva_in_non_executable_section(exec_data.data(), exec_data.size(), 0x10010));

    pe[0x84] = 0x4C;
    pe[0x85] = 0x01;
    CHECK_Q(parse_pe64_headers(pe.data(), pe.size(), info) == PeParseStatus::NotAmd64);
}

void test_profiles_and_public_registry() {
    using deflux::game::gta_sa_de::find_qualification_profile;
    using deflux::game::gta_sa_de::fingerprint_matches_profile;
    using deflux::game::gta_sa_de::is_supported_fingerprint;

    CHECK_Q(find_qualification_profile(kUnknownSha) == nullptr);
    const auto* profile = find_qualification_profile(kVanillaSha);
    CHECK_Q(profile != nullptr);
    CHECK_Q(profile->gworld_slot_rva == 0x05391B70u);
    CHECK_Q(fingerprint_matches_profile(*profile, kVanillaSha, 89497064ull, "1.0.113.21181"));
    CHECK_Q(!fingerprint_matches_profile(*profile, kUnknownSha, 89497064ull, "1.0.113.21181"));
    CHECK_Q(!fingerprint_matches_profile(*profile, kVanillaSha, 1, "1.0.113.21181"));
    CHECK_Q(!fingerprint_matches_profile(*profile, kVanillaSha, 89497064ull, "9.9.9"));
    CHECK_Q(!is_supported_fingerprint(kVanillaSha));
    CHECK_Q(!is_supported_fingerprint(kUnknownSha));
}

void test_anchor_evaluation() {
    using deflux::game::gta_sa_de::AnchorCheck;
    using deflux::game::gta_sa_de::evaluate_gworld_anchor;

    const std::uint64_t module = 0x140000000ull;
    const std::uint64_t size = 0x20000ull;
    const std::uint64_t heap = 0x0000000200001000ull;
    const std::uint64_t vtable = 0x140001000ull;

    CHECK_Q(evaluate_gworld_anchor(false, 0, false, 0, module, size) == AnchorCheck::SlotUnreadable);
    CHECK_Q(evaluate_gworld_anchor(true, 0, false, 0, module, size) == AnchorCheck::Null);
    CHECK_Q(evaluate_gworld_anchor(true, heap + 1, true, vtable, module, size) == AnchorCheck::Unaligned);
    CHECK_Q(evaluate_gworld_anchor(true, 0x10, true, vtable, module, size) == AnchorCheck::NonCanonical);
    CHECK_Q(evaluate_gworld_anchor(true, module + 0x100, true, vtable, module, size)
            == AnchorCheck::InsideModule);
    CHECK_Q(evaluate_gworld_anchor(true, heap, false, 0, module, size) == AnchorCheck::ObjectUnreadable);
    CHECK_Q(evaluate_gworld_anchor(true, heap, true, 0x10, module, size) == AnchorCheck::VtableNonCanonical);
    CHECK_Q(evaluate_gworld_anchor(true, heap, true, 0x0000000300000000ull, module, size)
            == AnchorCheck::VtableOutsideModule);
    CHECK_Q(evaluate_gworld_anchor(true, heap, true, vtable, module, size) == AnchorCheck::Ok);
}

void test_fail_closed_status_machine() {
    using deflux::game::gta_sa_de::AnchorCheck;
    using deflux::game::gta_sa_de::QualificationStatus;
    using deflux::game::gta_sa_de::result_from_checks;
    using deflux::game::gta_sa_de::status_from_checks;

    auto ok = passing_checks();
    CHECK_Q(status_from_checks(ok) == QualificationStatus::Qualified);
    const auto qualified = result_from_checks(ok, {});
    CHECK_Q(qualified.anchor_valid);
    CHECK_Q(qualified.module_valid);
    CHECK_Q(qualified.build_exact_match);

    auto missing = passing_checks();
    missing.running = false;
    CHECK_Q(status_from_checks(missing) == QualificationStatus::NotRunning);

    auto changed = passing_checks();
    changed.identity_stable = false;
    CHECK_Q(status_from_checks(changed) == QualificationStatus::ProcessChanged);

    auto mismatch = passing_checks();
    mismatch.fingerprint_matches_profile = false;
    CHECK_Q(status_from_checks(mismatch) == QualificationStatus::BuildMismatch);
    CHECK_Q(!result_from_checks(mismatch, {}).anchor_valid);

    auto no_profile = passing_checks();
    no_profile.profile_found = false;
    CHECK_Q(status_from_checks(no_profile) == QualificationStatus::ProfileMissing);

    auto bad_module = passing_checks();
    bad_module.module_ok = false;
    CHECK_Q(status_from_checks(bad_module) == QualificationStatus::ModuleInvalid);
    CHECK_Q(!result_from_checks(bad_module, {}).anchor_valid);

    auto bad_pe = passing_checks();
    bad_pe.pe_ok = false;
    CHECK_Q(status_from_checks(bad_pe) == QualificationStatus::ModuleInvalid);

    auto no_rva = passing_checks();
    no_rva.rva_in_image = false;
    CHECK_Q(status_from_checks(no_rva) == QualificationStatus::AnchorNotFound);

    auto exec_rva = passing_checks();
    exec_rva.rva_in_data_section = false;
    CHECK_Q(status_from_checks(exec_rva) == QualificationStatus::AnchorInvalid);

    auto null_anchor = passing_checks();
    null_anchor.anchor = AnchorCheck::Null;
    CHECK_Q(status_from_checks(null_anchor) == QualificationStatus::AnchorNotFound);

    auto invalid_anchor = passing_checks();
    invalid_anchor.anchor = AnchorCheck::InsideModule;
    CHECK_Q(status_from_checks(invalid_anchor) == QualificationStatus::AnchorInvalid);

    auto inspect = passing_checks();
    inspect.inspect_ok = false;
    CHECK_Q(status_from_checks(inspect) == QualificationStatus::Error);

    auto unread = passing_checks();
    unread.anchor = AnchorCheck::SlotUnreadable;
    CHECK_Q(status_from_checks(unread) == QualificationStatus::AnchorInvalid);
}

void test_observation_paths() {
    using deflux::core::ProcessLifecycle;
    using deflux::core::ProcessObservation;
    using deflux::game::gta_sa_de::QualificationStatus;
    using deflux::game::gta_sa_de::qualify_observation;

    ProcessObservation absent{};
    const auto not_running = qualify_observation(absent);
    CHECK_Q(not_running.status == QualificationStatus::NotRunning);
    CHECK_Q(!not_running.anchor_valid);

    ProcessObservation error{};
    error.state = ProcessLifecycle::ObservationError;
    error.message = "enumeration failed";
    const auto errored = qualify_observation(error);
    CHECK_Q(errored.status == QualificationStatus::Error);

    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "deflux_task6_qualification";
    fs::create_directories(dir);
    const auto file = dir / "not_gta.bin";
    {
        std::ofstream out(file, std::ios::binary);
        out << "not-a-supported-build";
    }

    ProcessObservation fake{};
    fake.state = ProcessLifecycle::ProcessDetected;
    fake.instance.process_id = 1;
    fake.instance.creation_identity = 1;
    fake.instance.executable_path = file.string();
    const auto unknown = qualify_observation(fake);
    CHECK_Q(unknown.status == QualificationStatus::ProfileMissing);
    CHECK_Q(!unknown.anchor_valid);
    CHECK_Q(!unknown.build_exact_match);

    fs::remove_all(dir);
}

void test_readiness_independent() {
    const deflux::game::gta_sa_de::GtaSaDeAdapter adapter;
    CHECK_Q(adapter.engine_ready().status == deflux::game::Readiness::NotImplemented);
    CHECK_Q(adapter.world_ready().status == deflux::game::Readiness::NotImplemented);
    CHECK_Q(adapter.local_player_ready().status == deflux::game::Readiness::NotImplemented);
    CHECK_Q(!deflux::game::succeeded(adapter.engine_ready()));
}

} // namespace

int run_qualification_tests(int& passed, int& failed) {
    g_passed = 0;
    g_failed = 0;
    test_status_names();
    test_complete_reads_and_pointers();
    test_pe_parser();
    test_profiles_and_public_registry();
    test_anchor_evaluation();
    test_fail_closed_status_machine();
    test_observation_paths();
    test_readiness_independent();
    passed += g_passed;
    failed += g_failed;
    return g_failed;
}

} // namespace deflux_tests
