#include "gworld_discovery.hpp"

#include "fingerprint.hpp"
#include "module_query.hpp"
#include "observation.hpp"
#include "pe_image.hpp"
#include "remote_read.hpp"
#include "win_utf8.hpp"

#include <cstring>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace deflux::game::gta_sa_de {
namespace {

struct LegacyPattern {
    std::uint8_t id;
    const std::uint8_t* bytes;
    const char* mask;
    std::uint32_t disp_at;
    std::uint32_t instr_len;
};

constexpr std::uint8_t kPatP1[] = {0x48, 0x8B, 0x05, 0, 0, 0, 0, 0x48, 0x85, 0xC0};
constexpr std::uint8_t kPatP5[] = {0x48, 0x8B, 0x05, 0, 0, 0, 0, 0x48, 0x85, 0xC0, 0x74};

constexpr LegacyPattern kLegacyPatterns[] = {
    {1, kPatP1, "xxx????xxx", 3, 7},
    {5, kPatP5, "xxx????xxxx", 3, 7},
};

std::size_t mask_length(const char* mask) noexcept {
    return mask != nullptr ? std::strlen(mask) : 0;
}

bool section_is_scannable(const PeSectionView& section) noexcept {
    return section.executable && section.virtual_size > 0;
}

GWorldDiscoveryResult finalize_unique_slots(GWorldDiscoveryResult result) {
    std::unordered_map<std::uint32_t, std::size_t> counts;
    for (const GWorldSignatureMatch& match : result.matches) {
        ++counts[match.slot_rva];
    }
    result.unique_slot_count = counts.size();
    if (counts.size() == 1) {
        result.unique_slot_rva = result.matches.front().slot_rva;
        result.status = GWorldDiscoveryStatus::Ok;
        result.message = "Exactly one unique GWorld slot RVA resolved from legacy signatures.";
        return result;
    }
    if (counts.empty()) {
        result.status = GWorldDiscoveryStatus::ZeroMatches;
        result.message = "No legacy GWorld signatures resolved to a slot RVA.";
        return result;
    }
    result.status = GWorldDiscoveryStatus::AmbiguousMatches;
    result.message = "Multiple unique slot RVAs resolved; refusing to pick one.";
    return result;
}

void scan_section_bytes(GWorldDiscoveryResult& result,
                        const std::uint8_t* blob,
                        std::size_t blob_size,
                        std::uint32_t section_rva) {
    for (const LegacyPattern& pattern : kLegacyPatterns) {
        const std::size_t pat_len = mask_length(pattern.mask);
        if (pat_len == 0 || blob_size < pat_len) {
            continue;
        }
        for (std::size_t i = 0; i + pat_len <= blob_size; ++i) {
            if (!match_signature(blob + i, pat_len, pattern.bytes, pattern.mask)) {
                continue;
            }
            const std::uint32_t instr_rva = section_rva + static_cast<std::uint32_t>(i);
            const std::uint32_t slot_rva = decode_x64_rip_relative(
                instr_rva, blob + i, pattern.disp_at, pattern.instr_len);
            if (slot_rva == 0) {
                continue;
            }
            result.matches.push_back(GWorldSignatureMatch{instr_rva, slot_rva, pattern.id});
        }
    }
}

} // namespace

bool match_signature(const std::uint8_t* data,
                     std::size_t size,
                     const std::uint8_t* pattern,
                     const char* mask) noexcept {
    if (data == nullptr || pattern == nullptr || mask == nullptr) {
        return false;
    }
    const std::size_t len = mask_length(mask);
    if (len == 0 || size < len) {
        return false;
    }
    for (std::size_t i = 0; i < len; ++i) {
        if (mask[i] == 'x' && data[i] != pattern[i]) {
            return false;
        }
    }
    return true;
}

std::uint32_t decode_x64_rip_relative(std::uint32_t instruction_rva,
                                      const std::uint8_t* instruction_bytes,
                                      std::uint32_t displacement_offset,
                                      std::uint32_t instruction_length) noexcept {
    if (instruction_bytes == nullptr) {
        return 0;
    }
    std::int32_t disp = 0;
    std::memcpy(&disp, instruction_bytes + displacement_offset, sizeof(disp));
    const std::uint64_t target =
        static_cast<std::uint64_t>(instruction_rva) + instruction_length + static_cast<std::int64_t>(disp);
    if (target > 0xFFFFFFFFull) {
        return 0;
    }
    return static_cast<std::uint32_t>(target);
}

bool pe_enumerate_sections(const std::uint8_t* headers,
                           std::size_t header_size,
                           std::vector<PeSectionView>& out) noexcept {
    out.clear();
    PeImageInfo info{};
    if (parse_pe64_headers(headers, header_size, info) != PeParseStatus::Ok) {
        return false;
    }

    std::uint32_t lfanew = 0;
    std::uint16_t optional_size = 0;
    if (!detail::read_u32(headers, header_size, 0x3C, lfanew)
        || !detail::read_u16(headers, header_size, lfanew + 20, optional_size)) {
        return false;
    }

    const std::size_t section_table = static_cast<std::size_t>(lfanew) + 24u + optional_size;
    constexpr std::size_t kSection = 40;
    for (std::uint16_t i = 0; i < info.number_of_sections; ++i) {
        const std::size_t off = section_table + static_cast<std::size_t>(i) * kSection;
        if (off + kSection > header_size) {
            return false;
        }
        PeSectionView section{};
        std::memcpy(section.name, headers + off, 8);
        section.name[8] = '\0';
        if (!detail::read_u32(headers, header_size, off + 8, section.virtual_size)
            || !detail::read_u32(headers, header_size, off + 12, section.virtual_address)
            || !detail::read_u32(headers, header_size, off + 16, section.raw_size)
            || !detail::read_u32(headers, header_size, off + 20, section.raw_pointer)
            || !detail::read_u32(headers, header_size, off + 36, section.characteristics)) {
            return false;
        }
        section.executable = (section.characteristics & detail::kScnMemExecute) != 0
            || std::strncmp(section.name, ".text", 5) == 0;
        out.push_back(section);
    }
    return !out.empty();
}

bool pe_section_for_rva(const std::vector<PeSectionView>& sections,
                        std::uint32_t rva,
                        PeSectionView& out) noexcept {
    for (const PeSectionView& section : sections) {
        const std::uint32_t span = section.virtual_size != 0 ? section.virtual_size : section.raw_size;
        if (span == 0) {
            continue;
        }
        if (rva >= section.virtual_address && rva < section.virtual_address + span) {
            out = section;
            return true;
        }
    }
    return false;
}

GWorldDiscoveryResult discover_gworld_slots_in_image(const std::uint8_t* image,
                                                     std::size_t image_size,
                                                     std::uint32_t size_of_image) noexcept {
    (void)size_of_image;
    GWorldDiscoveryResult result{};
    if (image == nullptr || image_size == 0) {
        result.message = "Image buffer is empty.";
        return result;
    }

    std::vector<PeSectionView> sections;
    if (!pe_enumerate_sections(image, image_size, sections)) {
        result.message = "Failed to enumerate PE sections.";
        return result;
    }

    bool scanned = false;
    for (const PeSectionView& section : sections) {
        if (!section_is_scannable(section)) {
            continue;
        }
        if (section.raw_pointer > image_size || section.raw_size > image_size - section.raw_pointer) {
            continue;
        }
        scanned = true;
        scan_section_bytes(result,
                           image + section.raw_pointer,
                           section.raw_size,
                           section.virtual_address);
    }
    if (!scanned) {
        result.status = GWorldDiscoveryStatus::NoExecutableSections;
        result.message = "No executable sections were available to scan.";
        return result;
    }
    return finalize_unique_slots(std::move(result));
}

QualificationInvestigation investigate_qualification(
    const deflux::core::ProcessObservation& observation) noexcept {
    QualificationInvestigation report{};
    report.qualification = qualify_observation(observation);

    if (report.qualification.status == QualificationStatus::EnvironmentContaminated) {
        report.summary = report.qualification.message;
        return report;
    }

    if (observation.state != deflux::core::ProcessLifecycle::ProcessDetected
        || !deflux::core::is_valid_process_instance(observation.instance)) {
        report.summary = "No verified process; investigation skipped.";
        return report;
    }

    const std::wstring path = detail::utf8_to_wide(observation.instance.executable_path);
    const auto fingerprint = detail::fingerprint_executable(path);
    const QualificationProfile* profile = fingerprint.ok
        ? find_qualification_profile(fingerprint.sha256_hex)
        : nullptr;

    std::ifstream file(path, std::ios::binary);
    if (file) {
        std::vector<std::uint8_t> disk((std::istreambuf_iterator<char>(file)),
                                       std::istreambuf_iterator<char>());
        if (!disk.empty()) {
            PeImageInfo pe{};
            if (parse_pe64_headers(disk.data(), disk.size(), pe) == PeParseStatus::Ok) {
                report.size_of_image = pe.size_of_image;
                report.offline_discovery =
                    discover_gworld_slots_in_image(disk.data(), disk.size(), pe.size_of_image);
            }
        }
    }

    detail::ScopedProcess process;
    if (!process.open_read_only(observation.instance.process_id)) {
        report.summary = "Could not open process for investigation.";
        return report;
    }

    const auto module = detail::query_main_module(observation.instance.process_id, path);
    if (!module.found) {
        report.summary = "Main module unavailable for investigation.";
        return report;
    }

    report.module_base = module.base;
    report.size_of_image = static_cast<std::uint32_t>(module.size);

    std::vector<std::uint8_t> headers(0x400, 0);
    if (!process.read_exact(module.base, headers.data(), headers.size())) {
        report.summary = "Failed to read module headers.";
        return report;
    }

    PeImageInfo pe{};
    PeParseStatus pe_status = parse_pe64_headers(headers.data(), headers.size(), pe);
    if (pe_status == PeParseStatus::Ok && pe.size_of_headers > headers.size() && pe.size_of_headers <= 0x2000u) {
        headers.assign(pe.size_of_headers, 0);
        if (process.read_exact(module.base, headers.data(), headers.size())) {
            pe = {};
            pe_status = parse_pe64_headers(headers.data(), headers.size(), pe);
        }
    }
    if (pe_status == PeParseStatus::Ok) {
        report.size_of_image = pe.size_of_image;
    }

    std::vector<PeSectionView> sections;
    if (pe_enumerate_sections(headers.data(), headers.size(), sections)) {
        PeSectionView profile_section{};
        if (profile != nullptr && pe_section_for_rva(sections, profile->gworld_slot_rva, profile_section)) {
            std::memset(report.profile_section, 0, sizeof(report.profile_section));
            std::memcpy(report.profile_section,
                        profile_section.name,
                        std::min(sizeof(report.profile_section) - 1, std::strlen(profile_section.name)));
        }

        GWorldDiscoveryResult live{};
        constexpr std::size_t kChunkSize = 0x100000; // 1 MiB bounded remote reads
        for (const PeSectionView& section : sections) {
            if (!section_is_scannable(section)) {
                continue;
            }
            const std::size_t read_size = section.virtual_size;
            if (read_size == 0) {
                continue;
            }
            std::vector<std::uint8_t> chunk(kChunkSize, 0);
            for (std::size_t offset = 0; offset < read_size; offset += kChunkSize) {
                const std::size_t this_read = std::min(kChunkSize, read_size - offset);
                if (!process.read_exact(module.base + section.virtual_address + offset,
                                        chunk.data(),
                                        this_read)) {
                    continue;
                }
                scan_section_bytes(live, chunk.data(), this_read, section.virtual_address + static_cast<std::uint32_t>(offset));
            }
        }
        report.live_discovery = finalize_unique_slots(std::move(live));
    }

    if (profile != nullptr) {
        report.profile_slot_read_ok =
            process.read_exact(module.base + profile->gworld_slot_rva,
                               &report.profile_slot_value,
                               sizeof(report.profile_slot_value));
    }

    const GWorldDiscoveryResult& sig =
        report.live_discovery.matches.empty() ? report.offline_discovery : report.live_discovery;
    report.signature_unique_slot_count = sig.unique_slot_count;
    report.signature_unique_slot_rva = sig.unique_slot_rva;
    if (profile != nullptr) {
        for (const GWorldSignatureMatch& match : sig.matches) {
            if (match.slot_rva == profile->gworld_slot_rva) {
                ++report.profile_rva_signature_hits;
            }
        }
        if (sig.unique_slot_count == 1) {
            report.profile_rva_matches_signature = profile->gworld_slot_rva == sig.unique_slot_rva;
        }
    }

    report.summary = report.qualification.message;
    if (profile != nullptr && report.profile_slot_read_ok && report.profile_slot_value == 0) {
        report.summary +=
            " Profile slot read succeeded but stored pointer is null (GWorld may be unset at probe time).";
    }
    if (sig.unique_slot_count == 1) {
        report.summary += report.profile_rva_matches_signature
            ? " Signature discovery resolves the same slot RVA as the profile."
            : " Signature discovery resolves a different slot RVA than the profile.";
    } else if (sig.status == GWorldDiscoveryStatus::AmbiguousMatches) {
        report.summary += " Signature discovery is ambiguous.";
    } else if (sig.status == GWorldDiscoveryStatus::ZeroMatches) {
        report.summary += " Signature discovery found zero slot RVAs.";
    }
    return report;
}

} // namespace deflux::game::gta_sa_de
