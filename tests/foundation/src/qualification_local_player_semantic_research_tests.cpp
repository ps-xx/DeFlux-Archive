#include "deflux/game/gta_sa_de/local_player_semantic_research.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdio>
#include <cstring>
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

#define CHECK_LP(expr) check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

struct SyntheticSemanticPeConfig {
    std::uint32_t image_size = 0x10000;
    std::uint32_t text_rva = 0x1000;
    std::uint32_t rdata_rva = 0x4000;
    std::uint32_t pdata_rva = 0x8000;
    std::uint32_t fn_begin = 0x1100;
    std::uint32_t fn_end = 0x1200;
    std::uint32_t ack_string_rva = 0x5000;
    std::uint32_t local_string_rva = 0x5100;
    std::uint32_t ack_lea_rva = 0x1110;
    std::uint32_t local_lea_rva = 0x1120;
    std::uint32_t global_rva = 0x6000;
    bool include_ack_string = true;
    bool include_local_string = true;
    bool include_ack_lea = true;
    bool include_local_lea = true;
    bool include_pdata = true;
};

void write_lea_rip(std::vector<std::uint8_t>& image, std::uint32_t instr_rva, std::uint32_t target_rva) {
    const std::int32_t disp = static_cast<std::int32_t>(target_rva - (instr_rva + 7));
    image[instr_rva + 0] = 0x48;
    image[instr_rva + 1] = 0x8D;
    image[instr_rva + 2] = 0x05;
    std::memcpy(image.data() + instr_rva + 3, &disp, sizeof(disp));
}

void write_mov_rip_global(std::vector<std::uint8_t>& image,
                          std::uint32_t instr_rva,
                          std::uint32_t global_rva) {
    const std::int32_t disp = static_cast<std::int32_t>(global_rva - (instr_rva + 7));
    image[instr_rva + 0] = 0x48;
    image[instr_rva + 1] = 0x8B;
    image[instr_rva + 2] = 0x05;
    std::memcpy(image.data() + instr_rva + 3, &disp, sizeof(disp));
}

void write_disp32_read(std::vector<std::uint8_t>& image,
                       std::uint32_t instr_rva,
                       std::uint32_t offset) {
    image[instr_rva + 0] = 0x48;
    image[instr_rva + 1] = 0x8B;
    image[instr_rva + 2] = 0x81;
    std::memcpy(image.data() + instr_rva + 3, &offset, sizeof(offset));
}

std::vector<std::uint8_t> build_synthetic_semantic_pe(const SyntheticSemanticPeConfig& cfg) {
    std::vector<std::uint8_t> image(cfg.image_size, 0);

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image.data());
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;

    const std::size_t nt_offset = static_cast<std::size_t>(dos->e_lfanew);
    *reinterpret_cast<std::uint32_t*>(image.data() + nt_offset) = IMAGE_NT_SIGNATURE;

    auto* file_header =
        reinterpret_cast<IMAGE_FILE_HEADER*>(image.data() + nt_offset + sizeof(std::uint32_t));
    file_header->Machine = IMAGE_FILE_MACHINE_AMD64;
    file_header->NumberOfSections = cfg.include_pdata ? 3 : 2;
    file_header->SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);

    auto* optional =
        reinterpret_cast<IMAGE_OPTIONAL_HEADER64*>(reinterpret_cast<std::uint8_t*>(file_header)
                                                 + sizeof(IMAGE_FILE_HEADER));
    optional->Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    optional->SectionAlignment = 0x1000;
    optional->FileAlignment = 0x200;
    optional->SizeOfImage = cfg.image_size;
    optional->SizeOfHeaders = 0x400;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image.data() + nt_offset);
    IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);

    auto setup_section = [&](IMAGE_SECTION_HEADER& section,
                             const char* name,
                             std::uint32_t rva,
                             std::uint32_t vsize,
                             std::uint32_t characteristics) {
        std::memset(section.Name, 0, sizeof(section.Name));
        std::memcpy(section.Name, name, std::strlen(name));
        section.VirtualAddress = rva;
        section.Misc.VirtualSize = vsize;
        section.SizeOfRawData = vsize;
        section.PointerToRawData = rva;
        section.Characteristics = characteristics;
    };

    setup_section(sections[0],
                  ".text",
                  cfg.text_rva,
                  0x3000,
                  IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ);
    setup_section(sections[1],
                  ".rdata",
                  cfg.rdata_rva,
                  0x3000,
                  IMAGE_SCN_MEM_READ);
    if (cfg.include_pdata) {
        setup_section(sections[2],
                      ".pdata",
                      cfg.pdata_rva,
                      0x1000,
                      IMAGE_SCN_MEM_READ);
    }

    if (cfg.include_ack_string) {
        constexpr char kAck[] = "AcknowledgedPawn";
        std::memcpy(image.data() + cfg.ack_string_rva, kAck, sizeof(kAck));
    }
    if (cfg.include_local_string) {
        constexpr char kLocal[] = "LocalPlayer";
        std::memcpy(image.data() + cfg.local_string_rva, kLocal, sizeof(kLocal));
    }

    if (cfg.include_ack_lea) {
        write_lea_rip(image, cfg.ack_lea_rva, cfg.ack_string_rva);
    }
    if (cfg.include_local_lea) {
        write_lea_rip(image, cfg.local_lea_rva, cfg.local_string_rva);
    }

    write_mov_rip_global(image, cfg.fn_begin + 0x30, cfg.global_rva);
    write_disp32_read(image, cfg.fn_begin + 0x40, 0x2A0);

    if (cfg.include_pdata) {
        const std::uint32_t entries = 100;
        for (std::uint32_t i = 0; i < entries; ++i) {
            const std::size_t off = static_cast<std::size_t>(cfg.pdata_rva) + i * 12;
            const std::uint32_t begin = (i == 0) ? cfg.fn_begin : cfg.pdata_rva + 0x200 + i * 0x10;
            const std::uint32_t end = (i == 0) ? cfg.fn_end : begin + 0x10;
            std::memcpy(image.data() + off, &begin, 4);
            std::memcpy(image.data() + off + 4, &end, 4);
            std::uint32_t unwind = 0;
            std::memcpy(image.data() + off + 8, &unwind, 4);
        }
    }

    return image;
}

void test_catalog_and_bounds_constants() {
    using deflux::game::gta_sa_de::kSemanticMaxDirectXrefs;
    using deflux::game::gta_sa_de::kSemanticMaxFunctionWindow;
    using deflux::game::gta_sa_de::kSemanticMaxGlobalRefsPerAnchor;
    using deflux::game::gta_sa_de::kSemanticMaxPdataFunctions;
    using deflux::game::gta_sa_de::semantic_anchor_catalog;

    CHECK_LP(kSemanticMaxDirectXrefs == 64);
    CHECK_LP(kSemanticMaxPdataFunctions == 64);
    CHECK_LP(kSemanticMaxFunctionWindow == 0x1000);
    CHECK_LP(kSemanticMaxGlobalRefsPerAnchor == 128);

    const auto catalog = semantic_anchor_catalog();
    CHECK_LP(catalog.size() == 6);
    CHECK_LP(std::strcmp(catalog[0].text, "AcknowledgedPawn") == 0);
    CHECK_LP(catalog[1].text != nullptr && std::strcmp(catalog[1].text, "LocalPlayer") == 0);
    CHECK_LP(catalog.back().secondary);
}

void test_multi_anchor_function_correlation() {
    using deflux::game::gta_sa_de::SemanticEvidenceClass;
    using deflux::game::gta_sa_de::analyze_local_player_semantic_image;

    SyntheticSemanticPeConfig cfg{};
    const std::vector<std::uint8_t> image = build_synthetic_semantic_pe(cfg);
    const auto summary = analyze_local_player_semantic_image(image.data(), image.size(), {});

    CHECK_LP(summary.sha_verified);
    CHECK_LP(summary.anchors.size() == 6);

    bool found_multi_anchor_fn = false;
    for (const auto& anchor : summary.anchors) {
        for (const auto& fn : anchor.functions) {
            if (fn.anchors_present.size() >= 2) {
                found_multi_anchor_fn = true;
                CHECK_LP(fn.classification == SemanticEvidenceClass::Strong
                         || fn.classification == SemanticEvidenceClass::PromisingUnverified);
            }
        }
    }
    CHECK_LP(found_multi_anchor_fn);
    CHECK_LP(summary.justified_live_candidate);
    CHECK_LP(summary.relationships.size() >= 1);
}

void test_string_and_xref_detection() {
    using deflux::game::gta_sa_de::analyze_local_player_semantic_image;

    SyntheticSemanticPeConfig cfg{};
    const std::vector<std::uint8_t> image = build_synthetic_semantic_pe(cfg);
    const auto summary = analyze_local_player_semantic_image(image.data(), image.size(), {});

    const deflux::game::gta_sa_de::SemanticAnchorAnalysis* ack = nullptr;
    const deflux::game::gta_sa_de::SemanticAnchorAnalysis* local = nullptr;
    for (const auto& anchor : summary.anchors) {
        if (std::strcmp(anchor.anchor, "AcknowledgedPawn") == 0) {
            ack = &anchor;
        } else if (std::strcmp(anchor.anchor, "LocalPlayer") == 0) {
            local = &anchor;
        }
    }
    CHECK_LP(ack != nullptr);
    CHECK_LP(local != nullptr);
    CHECK_LP(ack->string_occurrences >= 1);
    CHECK_LP(local->string_occurrences >= 1);
    CHECK_LP(ack->direct_xrefs >= 1);
    CHECK_LP(local->direct_xrefs >= 1);
}

void test_rejects_invalid_image() {
    using deflux::game::gta_sa_de::analyze_local_player_semantic_image;

    const auto summary = analyze_local_player_semantic_image(nullptr, 0, {});
    CHECK_LP(!summary.sha_verified);
    CHECK_LP(summary.message.find("too small") != std::string::npos
             || summary.message.find("Image buffer") != std::string::npos);
}

void test_no_live_candidate_without_multi_anchor() {
    using deflux::game::gta_sa_de::analyze_local_player_semantic_image;

    SyntheticSemanticPeConfig cfg{};
    cfg.include_local_lea = false;
    cfg.include_local_string = false;
    const std::vector<std::uint8_t> image = build_synthetic_semantic_pe(cfg);
    const auto summary = analyze_local_player_semantic_image(image.data(), image.size(), {});

    CHECK_LP(!summary.justified_live_candidate);
    CHECK_LP(summary.recommended_live_experiment.find("NO JUSTIFIED") != std::string::npos);
}

void test_format_report_lines() {
    using deflux::game::gta_sa_de::analyze_local_player_semantic_image;
    using deflux::game::gta_sa_de::format_local_player_semantic_report;

    SyntheticSemanticPeConfig cfg{};
    const std::vector<std::uint8_t> image = build_synthetic_semantic_pe(cfg);
    const auto summary = analyze_local_player_semantic_image(image.data(), image.size(), {});
    const auto lines = format_local_player_semantic_report(summary);
    CHECK_LP(!lines.empty());
    CHECK_LP(lines.front().find("Task #8E") != std::string::npos);
}

} // namespace

int run_qualification_local_player_semantic_research_tests(int& passed, int& failed) {
    test_catalog_and_bounds_constants();
    test_multi_anchor_function_correlation();
    test_string_and_xref_detection();
    test_rejects_invalid_image();
    test_no_live_candidate_without_multi_anchor();
    test_format_report_lines();
    passed += g_passed;
    failed += g_failed;
    return g_failed == 0 ? 0 : 1;
}

} // namespace deflux_tests
