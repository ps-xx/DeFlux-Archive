#include "deflux/game/gta_sa_de/gworld_discovery_research.hpp"
#include "deflux/game/gta_sa_de/gworld_identity_analysis.hpp"
#include "deflux/game/gta_sa_de/gworld_semantic_analysis.hpp"
#include "deflux/game/gta_sa_de/qualification.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
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

#define CHECK_DISC(expr) check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

bool write_u16(std::vector<std::uint8_t>& buf, std::size_t off, std::uint16_t value) {
    if (off + 2 > buf.size()) {
        return false;
    }
    buf[off] = static_cast<std::uint8_t>(value);
    buf[off + 1] = static_cast<std::uint8_t>(value >> 8);
    return true;
}

bool write_u32(std::vector<std::uint8_t>& buf, std::size_t off, std::uint32_t value) {
    if (off + 4 > buf.size()) {
        return false;
    }
    buf[off] = static_cast<std::uint8_t>(value);
    buf[off + 1] = static_cast<std::uint8_t>(value >> 8);
    buf[off + 2] = static_cast<std::uint8_t>(value >> 16);
    buf[off + 3] = static_cast<std::uint8_t>(value >> 24);
    return true;
}

std::vector<std::uint8_t> make_scan_image(std::uint32_t slot_rva) {
    std::vector<std::uint8_t> pe(0x3000, 0);
    pe[0] = 'M';
    pe[1] = 'Z';
    write_u32(pe, 0x3C, 0x80);
    pe[0x80] = 'P';
    pe[0x81] = 'E';
    write_u16(pe, 0x84, 0x8664);
    write_u16(pe, 0x86, 1);
    write_u16(pe, 0x94, 0xF0);
    write_u16(pe, 0x98, 0x020B);
    write_u32(pe, 0xD0, 0x3000);
    write_u32(pe, 0xD4, 0x200);

    const std::size_t sect0 = 0x188;
    std::memcpy(pe.data() + sect0, ".text\0\0", 6);
    write_u32(pe, sect0 + 8, 0x1000);
    write_u32(pe, sect0 + 12, 0x1000);
    write_u32(pe, sect0 + 16, 0x1000);
    write_u32(pe, sect0 + 20, 0x1000);
    write_u32(pe, sect0 + 36, 0x60000020);

    const std::uint32_t instr_rva = 0x1000;
    const std::uint32_t instr_len = 7;
    const std::int32_t disp = static_cast<std::int32_t>(slot_rva) - static_cast<std::int32_t>(instr_rva + instr_len);
    const std::size_t text_off = 0x1000;
    pe[text_off + 0] = 0x48;
    pe[text_off + 1] = 0x8B;
    pe[text_off + 2] = 0x05;
    write_u32(pe, text_off + 3, static_cast<std::uint32_t>(disp));
    pe[text_off + 7] = 0x48;
    pe[text_off + 8] = 0x85;
    pe[text_off + 9] = 0xC0;
    return pe;
}

void test_rip_relative_decode() {
    using deflux::game::gta_sa_de::decode_x64_rip_relative;
    const std::uint8_t instr[] = {0x48, 0x8B, 0x05, 0xF9, 0x0F, 0x00, 0x00, 0x48, 0x85, 0xC0};
    CHECK_DISC(decode_x64_rip_relative(0x1000, instr, 3, 7) == 0x2000u);
}

void test_zero_and_ambiguous_discovery() {
    using deflux::game::gta_sa_de::GWorldDiscoveryStatus;
    using deflux::game::gta_sa_de::discover_gworld_slots_in_image;

    auto no_text = make_scan_image(0x2500);
    no_text[0x1000] = 0x90;
    no_text[0x1001] = 0x90;
    const auto zero = discover_gworld_slots_in_image(no_text.data(), no_text.size(), 0x3000);
    CHECK_DISC(zero.status == GWorldDiscoveryStatus::ZeroMatches);

    auto single = make_scan_image(0x2500);
    const auto ok = discover_gworld_slots_in_image(single.data(), single.size(), 0x3000);
    CHECK_DISC(ok.status == GWorldDiscoveryStatus::Ok);
    CHECK_DISC(ok.unique_slot_count == 1);
    CHECK_DISC(ok.unique_slot_rva == 0x2500u);
    CHECK_DISC(!ok.matches.empty());

    auto ambiguous = make_scan_image(0x2500);
    const std::size_t text_off = 0x1100;
    ambiguous[text_off + 0] = 0x48;
    ambiguous[text_off + 1] = 0x8B;
    ambiguous[text_off + 2] = 0x05;
    write_u32(ambiguous, text_off + 3, static_cast<std::uint32_t>(0x2600 - (0x1100 + 7)));
    ambiguous[text_off + 7] = 0x48;
    ambiguous[text_off + 8] = 0x85;
    ambiguous[text_off + 9] = 0xC0;
    const auto many = discover_gworld_slots_in_image(ambiguous.data(), ambiguous.size(), 0x3000);
    CHECK_DISC(many.status == GWorldDiscoveryStatus::AmbiguousMatches);
    CHECK_DISC(many.unique_slot_count >= 2);
}

bool write_u64(std::vector<std::uint8_t>& buf, std::size_t off, std::uint64_t value) {
    if (off + 8 > buf.size()) {
        return false;
    }
    for (int i = 0; i < 8; ++i) {
        buf[off + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (8 * i));
    }
    return true;
}

bool pe_store(std::vector<std::uint8_t>& buf, std::size_t off, std::uint8_t value) {
    if (off >= buf.size()) {
        return false;
    }
    buf[off] = value;
    return true;
}

std::vector<std::uint8_t> make_research_pe(std::uint32_t slot_rva, std::size_t reference_count = 3) {
    constexpr std::size_t kPeSize = 0x5000;
    std::vector<std::uint8_t> pe(kPeSize, 0);
    pe[0] = 'M';
    pe[1] = 'Z';
    write_u32(pe, 0x3C, 0x80);
    pe[0x80] = 'P';
    pe[0x81] = 'E';
    write_u16(pe, 0x84, 0x8664);
    write_u16(pe, 0x86, 2);
    write_u16(pe, 0x94, 0xF0);
    write_u16(pe, 0x98, 0x020B);
    write_u64(pe, 0xB0, 0x140000000ull);
    write_u32(pe, 0xD0, static_cast<std::uint32_t>(kPeSize));
    write_u32(pe, 0xD4, 0x400);

    const std::size_t sect0 = 0x188;
    std::memcpy(pe.data() + sect0, ".text\0\0", 6);
    write_u32(pe, sect0 + 8, 0x1000);
    write_u32(pe, sect0 + 12, 0x1000);
    write_u32(pe, sect0 + 16, 0x1000);
    write_u32(pe, sect0 + 20, 0x1000);
    write_u32(pe, sect0 + 36, 0x60000020);

    const std::size_t sect1 = 0x188 + 40;
    std::memcpy(pe.data() + sect1, ".data\0\0\0", 7);
    write_u32(pe, sect1 + 8, 0x1000);
    write_u32(pe, sect1 + 12, 0x2000);
    write_u32(pe, sect1 + 16, 0x1000);
    write_u32(pe, sect1 + 20, 0x2000);
    write_u32(pe, sect1 + 36, 0xC0000040);

    auto emit = [&](std::uint32_t instr_rva) -> bool {
        if (instr_rva + 12 > pe.size()) {
            return false;
        }
        const std::int32_t disp =
            static_cast<std::int32_t>(slot_rva) - static_cast<std::int32_t>(instr_rva + 7);
        if (!pe_store(pe, instr_rva + 0, 0x48)
            || !pe_store(pe, instr_rva + 1, 0x8B)
            || !pe_store(pe, instr_rva + 2, 0x05)
            || !write_u32(pe, instr_rva + 3, static_cast<std::uint32_t>(disp))
            || !pe_store(pe, instr_rva + 7, 0x48)
            || !pe_store(pe, instr_rva + 8, 0x85)
            || !pe_store(pe, instr_rva + 9, 0xC0)
            || !pe_store(pe, instr_rva + 10, 0x74)
            || !pe_store(pe, instr_rva + 11, 0x10)) {
            return false;
        }
        return true;
    };

    for (std::size_t i = 0; i < reference_count; ++i) {
        emit(0x1000 + static_cast<std::uint32_t>(i * 0x20));
    }
    return pe;
}

void test_research_minimum_and_malformed_pe() {
    using deflux::game::gta_sa_de::decode_x64_rip_relative_mov;
    using deflux::game::gta_sa_de::GWorldReferenceEvidence;
    using deflux::game::gta_sa_de::run_gworld_discovery_research;

    const std::uint8_t minimum[] = {0x48, 0x8B, 0x05, 0xF9, 0x0F, 0x00, 0x00};
    GWorldReferenceEvidence minimum_ref{};
    CHECK_DISC(decode_x64_rip_relative_mov(minimum, sizeof(minimum), 0x1000, minimum_ref));
    CHECK_DISC(minimum_ref.slot_rva == 0x2000u);
    CHECK_DISC(!minimum_ref.has_null_test);
    CHECK_DISC(!minimum_ref.has_null_branch);

    GWorldReferenceEvidence empty_ref{};
    CHECK_DISC(!decode_x64_rip_relative_mov(nullptr, 0, 0x1000, empty_ref));
    CHECK_DISC(!decode_x64_rip_relative_mov(nullptr, 6, 0x1000, empty_ref));

    auto truncated_pe = make_research_pe(0x2010, 12);
    truncated_pe.resize(0x1040);
    const auto truncated_result = run_gworld_discovery_research(truncated_pe.data(), truncated_pe.size(), {});
    CHECK_DISC(truncated_result.candidates.empty());

    const auto sparse_pe = make_research_pe(0x2010, 3);
    const auto sparse = run_gworld_discovery_research(sparse_pe.data(), sparse_pe.size(), {});
    CHECK_DISC(sparse.candidates.empty());
    CHECK_DISC(sparse.stages.after_convergence == 0);
}

void test_research_decode_and_filters() {
    using deflux::game::gta_sa_de::decode_x64_rip_relative_mov;
    using deflux::game::gta_sa_de::GWorldReferenceEvidence;
    using deflux::game::gta_sa_de::run_gworld_discovery_research;

    const std::uint8_t bytes[] = {0x48, 0x8B, 0x05, 0xF9, 0x0F, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x74, 0x10};
    GWorldReferenceEvidence ref{};
    CHECK_DISC(decode_x64_rip_relative_mov(bytes, sizeof(bytes), 0x1000, ref));
    CHECK_DISC(ref.slot_rva == 0x2000u);
    CHECK_DISC(ref.has_null_test);
    CHECK_DISC(ref.has_null_branch);

    const std::uint8_t truncated[] = {0x48, 0x8B, 0x05, 0x01, 0x02, 0x03};
    GWorldReferenceEvidence truncated_ref{};
    CHECK_DISC(!decode_x64_rip_relative_mov(truncated, sizeof(truncated), 0x1000, truncated_ref));

    const std::uint8_t one_short[] = {0x48, 0x8B, 0x05, 0xF9, 0x0F, 0x00};
    GWorldReferenceEvidence one_short_ref{};
    CHECK_DISC(!decode_x64_rip_relative_mov(one_short, sizeof(one_short), 0x1000, one_short_ref));

    const auto pe = make_research_pe(0x2010, 12);
    const auto ok = run_gworld_discovery_research(pe.data(), pe.size(), {});
    CHECK_DISC(ok.shortlist_acceptable);
    if (ok.candidates.size() == 1) {
        CHECK_DISC(ok.candidates[0].slot_rva == 0x2010u);
        CHECK_DISC(ok.candidates[0].reference_count >= 12);
    } else {
        CHECK_DISC(false);
    }
}

void test_semantic_post_load_patterns() {
    using deflux::game::gta_sa_de::analyze_bounded_post_load_usage;
    using deflux::game::gta_sa_de::classify_semantic_candidate;
    using deflux::game::gta_sa_de::decode_mov_rip_dest_reg_index;
    using deflux::game::gta_sa_de::GWorldControlFlowEvidence;
    using deflux::game::gta_sa_de::GWorldNegativeEvidence;
    using deflux::game::gta_sa_de::GWorldObjectUsageEvidence;
    using deflux::game::gta_sa_de::GWorldReferenceDiversityEvidence;
    using deflux::game::gta_sa_de::GWorldSemanticClassification;
    using deflux::game::gta_sa_de::GWorldStructuralEvidence;

    const std::uint8_t object_use[] = {
        0x48, 0x85, 0xC0, 0x74, 0x10, 0x48, 0x8B, 0x48, 0x30,
    };
    const auto object_ev = analyze_bounded_post_load_usage(object_use, sizeof(object_use), 0);
    CHECK_DISC(object_ev.null_test);
    CHECK_DISC(object_ev.null_branch);
    CHECK_DISC(object_ev.post_load_dereference);
    CHECK_DISC(object_ev.field_access);

    const std::uint8_t scalar_use[] = {0x48, 0x85, 0xC0, 0x74, 0x05, 0x48, 0x83, 0xF8, 0x01};
    const auto scalar_ev = analyze_bounded_post_load_usage(scalar_use, sizeof(scalar_use), 0);
    CHECK_DISC(scalar_ev.scalar_like_usage);
    CHECK_DISC(!scalar_ev.post_load_dereference);

    const std::uint8_t truncated[] = {0x48, 0x85, 0xC0};
    const auto truncated_ev = analyze_bounded_post_load_usage(truncated, sizeof(truncated), 0);
    CHECK_DISC(truncated_ev.analysis_truncated);

    const std::uint8_t mov_load[] = {0x48, 0x8B, 0x05, 0x01, 0x02, 0x03, 0x04};
    CHECK_DISC(decode_mov_rip_dest_reg_index(mov_load, 3) == 0);

    GWorldStructuralEvidence structural{};
    structural.in_image = true;
    structural.in_pointer_storage_section = true;
    structural.aligned_8 = true;
    structural.non_iat = true;
    GWorldReferenceDiversityEvidence diversity{};
    diversity.unique_code_regions = 3;
    diversity.unique_pdata_functions = 2;
    diversity.pdata_boundaries_available = true;
    GWorldObjectUsageEvidence object_usage{};
    object_usage.refs_with_any_object_usage = 3;
    GWorldControlFlowEvidence control_flow{};
    control_flow.refs_with_null_test = 3;
    control_flow.refs_with_null_branch = 3;
    GWorldNegativeEvidence negative{};
    std::string rationale;
    CHECK_DISC(classify_semantic_candidate(
                   structural, diversity, object_usage, control_flow, negative, rationale)
               == GWorldSemanticClassification::GWorldLike);

    diversity.unique_code_regions = 1;
    diversity.references_concentrated_in_one_region = true;
    object_usage.refs_with_any_object_usage = 1;
    CHECK_DISC(classify_semantic_candidate(
                   structural, diversity, object_usage, control_flow, negative, rationale)
               == GWorldSemanticClassification::Ambiguous);
}

std::vector<std::uint8_t> make_semantic_pe(const std::uint8_t* post_pattern,
                                           std::size_t post_len,
                                           std::size_t ref_count,
                                           std::uint32_t slot_rva,
                                           std::uint32_t region_stride) {
    auto pe = make_research_pe(slot_rva, 0);
    const std::size_t sect0 = 0x188;
    const std::uint32_t text_span =
        0x1000 + static_cast<std::uint32_t>(ref_count * region_stride) + 0x100;
    write_u32(pe, sect0 + 8, text_span);
    write_u32(pe, sect0 + 16, text_span);
    const std::size_t required_size = static_cast<std::size_t>(text_span) + 0x3000;
    if (pe.size() < required_size) {
        pe.resize(required_size, 0);
    }
    write_u32(pe, 0xD0, static_cast<std::uint32_t>(required_size));
    auto emit_with_post = [&](std::uint32_t instr_rva) {
        if (instr_rva + 7 + post_len > pe.size()) {
            return;
        }
        const std::int32_t disp =
            static_cast<std::int32_t>(slot_rva) - static_cast<std::int32_t>(instr_rva + 7);
        pe[instr_rva + 0] = 0x48;
        pe[instr_rva + 1] = 0x8B;
        pe[instr_rva + 2] = 0x05;
        write_u32(pe, instr_rva + 3, static_cast<std::uint32_t>(disp));
        for (std::size_t i = 0; i < post_len; ++i) {
            pe[instr_rva + 7 + i] = post_pattern[i];
        }
    };
    for (std::size_t i = 0; i < ref_count; ++i) {
        emit_with_post(0x1000 + static_cast<std::uint32_t>(i * region_stride));
    }
    return pe;
}

void test_semantic_research_aggregation() {
    using deflux::game::gta_sa_de::GWorldSemanticClassification;
    using deflux::game::gta_sa_de::run_gworld_discovery_research;

    const auto pe = make_research_pe(0x2010, 12);
    const auto result = run_gworld_discovery_research(pe.data(), pe.size(), {});
    CHECK_DISC(result.stages.after_convergence == 1);
    CHECK_DISC(result.semantic.summary.converged_dataset_count == 1);
    if (result.semantic.converged_records.size() == 1) {
        const auto& record = result.semantic.converged_records[0];
        CHECK_DISC(record.slot_rva == 0x2010u);
        CHECK_DISC(record.diversity.context_qualified_references >= 12);
        CHECK_DISC(record.diversity.unique_instruction_rvas >= 12);
        CHECK_DISC(record.convergence_dataset_member);
        CHECK_DISC(record.classification == GWorldSemanticClassification::Unlikely);
    } else {
        CHECK_DISC(false);
    }

    const std::uint8_t scalar_post[] = {0x48, 0x85, 0xC0, 0x74, 0x05, 0x48, 0x83, 0xF8, 0x01};
    const auto scalar_ev = deflux::game::gta_sa_de::analyze_bounded_post_load_usage(
        scalar_post, sizeof(scalar_post), 0);
    CHECK_DISC(scalar_ev.scalar_like_usage);
    CHECK_DISC(!scalar_ev.post_load_dereference);
}

void test_identity_xref_correlation() {
    using deflux::game::gta_sa_de::classify_identity_evidence;
    using deflux::game::gta_sa_de::decode_x64_rip_relative_lea_or_mov;
    using deflux::game::gta_sa_de::GWorldIdentityClassification;

    const std::uint8_t lea[] = {0x48, 0x8D, 0x0D, 0x09, 0x10, 0x00, 0x00};
    std::uint32_t target = 0;
    CHECK_DISC(decode_x64_rip_relative_lea_or_mov(lea, sizeof(lea), 0x1000, target));
    CHECK_DISC(target == 0x2010u);

    const std::uint8_t truncated[] = {0x48, 0x8D, 0x0D, 0x01};
    CHECK_DISC(!decode_x64_rip_relative_lea_or_mov(truncated, sizeof(truncated), 0x1000, target));

    std::string rationale;
    CHECK_DISC(classify_identity_evidence(1, 1, 0, 0, rationale)
               == GWorldIdentityClassification::Supported);
    CHECK_DISC(classify_identity_evidence(0, 0, 1, 3, rationale)
               == GWorldIdentityClassification::Contradicted);
    CHECK_DISC(classify_identity_evidence(0, 0, 0, 0, rationale)
               == GWorldIdentityClassification::Ambiguous);
}

void test_offline_vanilla_exe_scan_if_present() {
    using deflux::game::gta_sa_de::GWorldDiscoveryStatus;
    using deflux::game::gta_sa_de::discover_gworld_slots_in_image;
    using deflux::game::gta_sa_de::parse_pe64_headers;
    using deflux::game::gta_sa_de::PeImageInfo;

    const char* path =
        "C:\\Program Files (x86)\\Steam\\steamapps\\common\\GTA San Andreas - The Definitive Edition\\"
        "Gameface\\Binaries\\Win64\\SanAndreas.exe";
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::printf("SKIP offline vanilla exe scan (SanAndreas.exe not found)\n");
        ++g_passed;
        return;
    }

    std::vector<std::uint8_t> disk((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    PeImageInfo pe{};
    CHECK_DISC(parse_pe64_headers(disk.data(), disk.size(), pe) == deflux::game::gta_sa_de::PeParseStatus::Ok);
    const auto discovery = discover_gworld_slots_in_image(disk.data(), disk.size(), pe.size_of_image);
    CHECK_DISC(discovery.matches.size() > 0);
    std::printf("INFO offline signature matches=%zu unique_slots=%zu status=%u profile_rva_hits=%zu\n",
                discovery.matches.size(),
                discovery.unique_slot_count,
                static_cast<unsigned>(discovery.status),
                std::count_if(discovery.matches.begin(),
                              discovery.matches.end(),
                              [](const deflux::game::gta_sa_de::GWorldSignatureMatch& m) {
                                  return m.slot_rva == 0x05391B70u;
                              }));
    CHECK_DISC(discovery.status == GWorldDiscoveryStatus::AmbiguousMatches
               || discovery.status == GWorldDiscoveryStatus::Ok);
}

} // namespace

int run_qualification_discovery_tests(int& passed, int& failed) {
    g_passed = 0;
    g_failed = 0;
    test_rip_relative_decode();
    test_zero_and_ambiguous_discovery();
    test_research_minimum_and_malformed_pe();
    test_research_decode_and_filters();
    test_semantic_post_load_patterns();
    test_semantic_research_aggregation();
    test_identity_xref_correlation();
    test_offline_vanilla_exe_scan_if_present();
    passed += g_passed;
    failed += g_failed;
    return g_failed;
}

} // namespace deflux_tests
