#include "deflux/game/gta_sa_de/gworld_discovery_research.hpp"

#include "gworld_discovery.hpp"
#include "pe_image.hpp"
#include "sha256.hpp"
#include "win_utf8.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <unordered_map>

namespace deflux::game::gta_sa_de {
namespace {

constexpr std::size_t kMaxShortlist = 20;
constexpr std::size_t kMinConvergingReferences = 12;
constexpr std::size_t kMaxReferencesPerCandidate = 8;

constexpr std::uint32_t kScnMemWrite = 0x80000000u;
constexpr std::uint32_t kScnMemRead = 0x40000000u;

const char* reg_name_from_modrm_reg(std::uint8_t modrm, bool rex_b) noexcept {
    static constexpr const char* kNames[] = {
        "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    };
    const unsigned reg = ((modrm >> 3) & 7u) + (rex_b ? 8u : 0u);
    return reg < 16 ? kNames[reg] : "?";
}

bool is_rip_relative_modrm(std::uint8_t modrm) noexcept {
    return (modrm & 0xC7u) == 0x05u;
}

bool section_name_is_rejected(const char* name) noexcept {
    static constexpr const char* kRejected[] = {
        ".text", ".pdata", ".xdata", ".reloc", ".rsrc", ".idata", ".edata", ".debug", ".CRT", ".tls",
    };
    for (const char* rejected : kRejected) {
        if (std::strncmp(name, rejected, 8) == 0) {
            return true;
        }
    }
    return false;
}

PeSectionSummary summarize_section(const PeSectionView& section) {
    PeSectionSummary out{};
    std::memcpy(out.name, section.name, 8);
    out.name[8] = '\0';
    out.virtual_address = section.virtual_address;
    out.virtual_size = section.virtual_size != 0 ? section.virtual_size : section.raw_size;
    out.characteristics = section.characteristics;
    out.executable = section.executable
        || (section.characteristics & detail::kScnMemExecute) != 0;
    out.writable = (section.characteristics & kScnMemWrite) != 0;
    out.pointer_storage_plausible =
        !out.executable && !section_name_is_rejected(out.name)
        && (std::strncmp(out.name, ".data", 5) == 0 || std::strncmp(out.name, ".bss", 4) == 0);
    return out;
}

bool pe_image_base(const std::uint8_t* headers, std::size_t header_size, std::uint64_t& out) noexcept {
    std::uint32_t lfanew = 0;
    if (!detail::read_u32(headers, header_size, 0x3C, lfanew)) {
        return false;
    }
    std::uint64_t base = 0;
    for (int i = 0; i < 8; ++i) {
        std::uint8_t byte = 0;
        if (lfanew + 24 + 24 + i >= header_size) {
            return false;
        }
        byte = headers[lfanew + 24 + 24 + i];
        base |= static_cast<std::uint64_t>(byte) << (8 * i);
    }
    out = base;
    return true;
}

bool read_import_directory(const std::uint8_t* headers,
                           std::size_t header_size,
                           std::uint32_t& import_rva,
                           std::uint32_t& import_size) noexcept {
    import_rva = 0;
    import_size = 0;
    std::uint32_t lfanew = 0;
    std::uint16_t magic = 0;
    if (!detail::read_u32(headers, header_size, 0x3C, lfanew)
        || !detail::read_u16(headers, header_size, lfanew + 24, magic)
        || magic != 0x020Bu) {
        return false;
    }
    const std::size_t import_dir_off = static_cast<std::size_t>(lfanew) + 24u + 112u + 8u;
    return detail::read_u32(headers, header_size, import_dir_off, import_rva)
        && detail::read_u32(headers, header_size, import_dir_off + 4, import_size);
}

bool rva_in_range(std::uint32_t rva, std::uint32_t begin, std::uint32_t size) noexcept {
    return size > 0 && rva >= begin && rva < begin + size;
}

bool bytes_have_null_test(const std::uint8_t* tail, std::size_t tail_len) noexcept {
    for (std::size_t i = 0; i + 3 <= tail_len; ++i) {
        if (tail[i] == 0x48 && tail[i + 1] == 0x85) {
            const std::uint8_t modrm = tail[i + 2];
            if ((modrm & 0xC0u) == 0xC0u) {
                const unsigned left = (modrm >> 3) & 7u;
                const unsigned right = modrm & 7u;
                if (left == right) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool bytes_have_null_branch(const std::uint8_t* tail, std::size_t tail_len) noexcept {
    for (std::size_t i = 0; i < tail_len; ++i) {
        if (tail[i] == 0x74 || tail[i] == 0x75) {
            return true;
        }
        if (i + 1 < tail_len && tail[i] == 0x0F && (tail[i + 1] == 0x84 || tail[i + 1] == 0x85)) {
            return true;
        }
    }
    return false;
}

void capture_context(GWorldReferenceEvidence& ref, const std::uint8_t* bytes, std::size_t available) {
    const std::size_t instr_store = std::min<std::size_t>(ref.instruction_size, sizeof(ref.instruction_bytes));
    std::memset(ref.instruction_bytes, 0, sizeof(ref.instruction_bytes));
    if (instr_store > 0) {
        std::memcpy(ref.instruction_bytes, bytes, instr_store);
    }

    const std::uint8_t* tail = bytes + ref.instruction_size;
    const std::size_t tail_available =
        (available > ref.instruction_size) ? (available - ref.instruction_size) : 0;
    const std::size_t tail_store = std::min(tail_available, sizeof(ref.context_bytes));
    std::memset(ref.context_bytes, 0, sizeof(ref.context_bytes));
    if (tail_store > 0) {
        std::memcpy(ref.context_bytes, tail, tail_store);
    }
    ref.has_null_test = bytes_have_null_test(ref.context_bytes, tail_store);
    ref.has_null_branch = bytes_have_null_branch(ref.context_bytes, tail_store);
}

bool fill_section_name(const std::vector<PeSectionSummary>& sections,
                       std::uint32_t rva,
                       char out[9]) {
    for (const PeSectionSummary& section : sections) {
        if (section.virtual_size == 0) {
            continue;
        }
        if (rva >= section.virtual_address && rva < section.virtual_address + section.virtual_size) {
            std::memset(out, 0, 9);
            std::memcpy(out, section.name, std::min<std::size_t>(8, std::strlen(section.name)));
            return true;
        }
    }
    std::memset(out, 0, 9);
    return false;
}

} // namespace

bool section_is_pointer_storage_plausible(const PeSectionSummary& section) noexcept {
    return section.pointer_storage_plausible;
}

bool rva_in_import_directory(const std::uint8_t* headers,
                             std::size_t header_size,
                             std::uint32_t rva) noexcept {
    std::uint32_t import_rva = 0;
    std::uint32_t import_size = 0;
    if (!read_import_directory(headers, header_size, import_rva, import_size)) {
        return false;
    }
    return rva_in_range(rva, import_rva, import_size);
}

bool decode_x64_rip_relative_mov(const std::uint8_t* bytes,
                                 std::size_t available,
                                 std::uint32_t instruction_rva,
                                 GWorldReferenceEvidence& out) noexcept {
    out = GWorldReferenceEvidence{};
    if (bytes == nullptr || available < 7 || (bytes[0] != 0x48 && bytes[0] != 0x4C) || bytes[1] != 0x8B) {
        return false;
    }
    if ((bytes[2] & 0xC7u) != 0x05u) {
        return false;
    }

    constexpr std::uint32_t kInstrLen = 7;
    const std::uint32_t slot_rva =
        decode_x64_rip_relative(instruction_rva, bytes, 3, kInstrLen);
    if (slot_rva == 0) {
        return false;
    }

    out.instruction_rva = instruction_rva;
    out.slot_rva = slot_rva;
    out.form = GWorldReferenceForm::MovRegFromRip;
    {
        static constexpr const char* kNames[] = {
            "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
            "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
        };
        const unsigned reg = decode_mov_rip_dest_reg_index(bytes, available);
        const char* name = reg < 16 ? kNames[reg] : "?";
        std::memset(out.dest_reg, 0, sizeof(out.dest_reg));
        std::memcpy(out.dest_reg, name, std::min<std::size_t>(sizeof(out.dest_reg) - 1, std::strlen(name)));
    }
    out.instruction_size = kInstrLen;
    std::memcpy(out.instruction_bytes, bytes, std::min<std::size_t>(kInstrLen, sizeof(out.instruction_bytes)));
    if (available > kInstrLen) {
        const std::size_t tail_len = std::min(available - kInstrLen, sizeof(out.context_bytes));
        std::memcpy(out.context_bytes, bytes + kInstrLen, tail_len);
        out.has_null_test = bytes_have_null_test(out.context_bytes, tail_len);
        out.has_null_branch = bytes_have_null_branch(out.context_bytes, tail_len);
    }
    return true;
}

GWorldDiscoveryResearchResult run_gworld_discovery_research(const std::uint8_t* image,
                                                            std::size_t image_size,
                                                            std::string_view expected_sha256_hex) {
    GWorldDiscoveryResearchResult result{};
    if (image == nullptr || image_size == 0) {
        result.message = "Image buffer is empty.";
        return result;
    }

    PeImageInfo pe{};
    if (parse_pe64_headers(image, image_size, pe) != PeParseStatus::Ok) {
        result.message = "Failed to parse PE headers.";
        return result;
    }
    result.size_of_image = pe.size_of_image;
    (void)pe_image_base(image, image_size, result.image_base);

    std::vector<PeSectionView> raw_sections;
    if (!pe_enumerate_sections(image, image_size, raw_sections)) {
        result.message = "Failed to enumerate PE sections.";
        return result;
    }
    for (const PeSectionView& section : raw_sections) {
        result.sections.push_back(summarize_section(section));
    }

    if (!expected_sha256_hex.empty()) {
        result.sha_verified =
            detail::sha256_buffer(image, image_size, result.sha256_hex)
            && result.sha256_hex == expected_sha256_hex;
        if (!result.sha_verified) {
            result.message = "Executable SHA-256 does not match expected build fingerprint.";
            return result;
        }
    }

    std::unordered_map<std::uint32_t, std::vector<GWorldReferenceEvidence>> grouped;
    std::unordered_map<std::uint32_t, std::size_t> reference_counts;
    std::unordered_map<std::uint32_t, std::size_t> raw_reference_counts;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> context_instruction_rvas;
    std::unordered_map<std::uint32_t, std::array<char, 9>> slot_dest_sections;
    std::unordered_map<std::uint32_t, GWorldLegacySlotAudit> legacy_audits;
    const auto note_legacy_stage = [&](std::uint32_t slot_rva, std::size_t GWorldLegacySlotAudit::*field) {
        if (slot_rva != 0x05391B70u && slot_rva != 0x0FE1D1D8u) {
            return;
        }
        auto& audit = legacy_audits[slot_rva];
        audit.slot_rva = slot_rva;
        audit.*field += 1;
    };

    auto try_reference_at = [&](const std::uint8_t* blob,
                                std::size_t blob_size,
                                std::size_t i,
                                std::uint32_t section_rva) {
        GWorldReferenceEvidence ref{};
        if (!decode_x64_rip_relative_mov(blob + i,
                                         blob_size - i,
                                         section_rva + static_cast<std::uint32_t>(i),
                                         ref)) {
            return;
        }
        ++result.stages.raw_references;
        ++raw_reference_counts[ref.slot_rva];
        note_legacy_stage(ref.slot_rva, &GWorldLegacySlotAudit::raw_references);

        if (ref.slot_rva + 8u > result.size_of_image) {
            return;
        }
        ++result.stages.after_image_bounds;
        note_legacy_stage(ref.slot_rva, &GWorldLegacySlotAudit::after_image_bounds);

        PeSectionSummary dest{};
        bool found = false;
        for (const PeSectionSummary& summary : result.sections) {
            if (summary.virtual_size != 0 && ref.slot_rva >= summary.virtual_address
                && ref.slot_rva < summary.virtual_address + summary.virtual_size) {
                dest = summary;
                found = true;
                break;
            }
        }
        if (!found || !section_is_pointer_storage_plausible(dest)) {
            return;
        }
        ++result.stages.after_data_section;
        note_legacy_stage(ref.slot_rva, &GWorldLegacySlotAudit::after_data_section);
        std::memset(ref.dest_section, 0, sizeof(ref.dest_section));
        std::memcpy(ref.dest_section, dest.name, std::min<std::size_t>(8, sizeof(ref.dest_section) - 1));

        if ((ref.slot_rva & 7u) != 0) {
            return;
        }
        ++result.stages.after_alignment;
        note_legacy_stage(ref.slot_rva, &GWorldLegacySlotAudit::after_alignment);

        if (rva_in_import_directory(image, image_size, ref.slot_rva)) {
            return;
        }
        ++result.stages.after_not_import;
        note_legacy_stage(ref.slot_rva, &GWorldLegacySlotAudit::after_not_import);

        if (!ref.has_null_test || !ref.has_null_branch) {
            return;
        }
        ++result.stages.after_context;
        note_legacy_stage(ref.slot_rva, &GWorldLegacySlotAudit::after_context);

        fill_section_name(result.sections, ref.instruction_rva, ref.source_section);
        ++reference_counts[ref.slot_rva];
        context_instruction_rvas[ref.slot_rva].push_back(ref.instruction_rva);
        if (slot_dest_sections.find(ref.slot_rva) == slot_dest_sections.end()) {
            std::array<char, 9> dest_name{};
            std::memcpy(dest_name.data(), ref.dest_section, 9);
            slot_dest_sections[ref.slot_rva] = dest_name;
        }
        auto& refs = grouped[ref.slot_rva];
        if (refs.size() < kMaxReferencesPerCandidate) {
            refs.push_back(ref);
        }
    };

    for (const PeSectionView& section : raw_sections) {
        if (!section.executable || section.raw_size == 0) {
            continue;
        }
        if (section.raw_pointer > image_size || section.raw_size > image_size - section.raw_pointer) {
            continue;
        }
        result.stages.executable_bytes_scanned += section.raw_size;
        const std::uint8_t* blob = image + section.raw_pointer;
        const std::size_t blob_size = section.raw_size;
        const std::uint8_t* cursor = blob;
        const std::uint8_t* const end = blob + (blob_size > 7 ? blob_size - 7 : 0);
        while (cursor < end) {
            const std::size_t i = static_cast<std::size_t>(cursor - blob);
            const std::uint8_t b0 = *cursor;
            if (b0 != 0x48 && b0 != 0x4C) {
                ++cursor;
                continue;
            }
            if (cursor + 1 >= end || cursor[1] != 0x8B) {
                ++cursor;
                continue;
            }
            try_reference_at(blob, blob_size, i, section.virtual_address);
            cursor += 7;
        }
    }

    std::vector<GWorldSlotCandidate> converged;
    for (auto& entry : grouped) {
        auto& refs = entry.second;
        const std::size_t total_refs = reference_counts[entry.first];
        if (total_refs < kMinConvergingReferences) {
            continue;
        }
        if (refs.empty()) {
            continue;
        }

        std::sort(refs.begin(), refs.end(), [](const GWorldReferenceEvidence& a, const GWorldReferenceEvidence& b) {
            return a.instruction_rva < b.instruction_rva;
        });

        GWorldSlotCandidate candidate{};
        candidate.slot_rva = entry.first;
        candidate.reference_count = total_refs;
        std::memset(candidate.dest_section, 0, sizeof(candidate.dest_section));
        std::memcpy(candidate.dest_section,
                     refs.front().dest_section,
                     std::min<std::size_t>(8, sizeof(candidate.dest_section) - 1));
        candidate.survival_reasons = {
            "source_in_executable_section",
            "slot_within_image",
            "slot_in_pointer_storage_section",
            "slot_8byte_aligned",
            "slot_not_in_import_directory",
            "post_load_null_test_present",
            "post_load_null_branch_present",
            "converging_code_references>=" + std::to_string(kMinConvergingReferences),
        };
        const std::size_t keep = std::min(kMaxReferencesPerCandidate, refs.size());
        candidate.references.assign(refs.begin(), refs.begin() + static_cast<std::ptrdiff_t>(keep));
        if (entry.first == 0x05391B70u || entry.first == 0x0FE1D1D8u) {
            legacy_audits[entry.first].slot_rva = entry.first;
            legacy_audits[entry.first].reached_convergence = true;
        }
        converged.push_back(std::move(candidate));
    }
    result.stages.after_convergence = converged.size();
    for (const std::uint32_t slot : {0x05391B70u, 0x0FE1D1D8u}) {
        const auto it = legacy_audits.find(slot);
        if (it != legacy_audits.end()) {
            result.legacy_slot_audits.push_back(it->second);
        } else {
            GWorldLegacySlotAudit absent{};
            absent.slot_rva = slot;
            result.legacy_slot_audits.push_back(absent);
        }
    }

    std::sort(converged.begin(), converged.end(), [](const GWorldSlotCandidate& a, const GWorldSlotCandidate& b) {
        if (a.reference_count != b.reference_count) {
            return a.reference_count > b.reference_count;
        }
        return a.slot_rva < b.slot_rva;
    });

    result.semantic = analyze_semantic_candidates(image,
                                                  image_size,
                                                  result.sections,
                                                  reference_counts,
                                                  context_instruction_rvas,
                                                  slot_dest_sections,
                                                  raw_reference_counts,
                                                  kMinConvergingReferences);
    result.identity = analyze_identity_evidence(image, image_size, result.semantic);

    if (converged.size() > kMaxShortlist) {
        result.candidates.clear();
        result.stages.final_shortlist = converged.size();
        result.shortlist_acceptable = false;
        result.message =
            "Static evidence insufficient: " + std::to_string(converged.size())
            + " converged candidates exceed review threshold (" + std::to_string(kMaxShortlist) + "). "
            + result.semantic.summary.message + " " + result.identity.summary.message;
        return result;
    }

    result.candidates = std::move(converged);
    result.stages.final_shortlist = result.candidates.size();
    result.shortlist_acceptable = true;
    result.message = result.candidates.empty()
        ? "No candidates survived conservative filtering."
        : "Reviewable shortlist produced.";
    if (!result.semantic.summary.message.empty()) {
        result.message += " " + result.semantic.summary.message;
    }
    return result;
}

GWorldDiscoveryResearchResult run_gworld_discovery_research_file(
    std::wstring_view path,
    std::string_view expected_sha256_hex) {
    GWorldDiscoveryResearchResult result{};
    if (path.empty()) {
        result.message = "Executable path is empty.";
        return result;
    }

    const std::wstring wide(path);
    HANDLE file = CreateFileW(
        wide.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        result.message = "Could not open executable for research scan.";
        return result;
    }

    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
        CloseHandle(file);
        result.message = "Could not map executable for research scan.";
        return result;
    }

    const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (view == nullptr) {
        CloseHandle(mapping);
        CloseHandle(file);
        result.message = "Could not map view of executable.";
        return result;
    }

    LARGE_INTEGER file_size{};
    if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart <= 0) {
        UnmapViewOfFile(view);
        CloseHandle(mapping);
        CloseHandle(file);
        result.message = "Executable size is invalid.";
        return result;
    }

    result = run_gworld_discovery_research(
        static_cast<const std::uint8_t*>(view),
        static_cast<std::size_t>(file_size.QuadPart),
        expected_sha256_hex);

    UnmapViewOfFile(view);
    CloseHandle(mapping);
    CloseHandle(file);
    return result;
}

} // namespace deflux::game::gta_sa_de
