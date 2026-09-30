#include "deflux/game/gta_sa_de/local_player_semantic_research.hpp"

#include "deflux/game/gta_sa_de/gworld_identity_analysis.hpp"
#include "deflux/game/gta_sa_de/qualification.hpp"
#include "fingerprint.hpp"
#include "gworld_discovery.hpp"
#include "pe_image.hpp"
#include "sha256.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace deflux::game::gta_sa_de {
namespace {

constexpr char kRegNames[][4] = {
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
};

struct RuntimeFunctionRange {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

bool read_u32_local(const std::uint8_t* data, std::size_t size, std::size_t offset, std::uint32_t& out) {
    if (data == nullptr || offset + 4 > size) {
        return false;
    }
    out = static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24);
    return true;
}

bool section_is_readonly_data(const PeSectionView& section) noexcept {
    if (section.executable) {
        return false;
    }
    return std::strncmp(section.name, ".rdata", 6) == 0
        || std::strncmp(section.name, ".data", 5) == 0
        || std::strncmp(section.name, "_RDATA", 6) == 0;
}

bool read_bytes_at_rva(const std::uint8_t* image,
                       std::size_t image_size,
                       const std::vector<PeSectionView>& sections,
                       std::uint32_t rva,
                       std::uint8_t* out,
                       std::size_t out_len,
                       std::size_t& written) noexcept {
    written = 0;
    if (image == nullptr || out == nullptr || out_len == 0) {
        return false;
    }
    for (const PeSectionView& section : sections) {
        const std::uint32_t span = section.virtual_size != 0 ? section.virtual_size : section.raw_size;
        if (span == 0 || rva < section.virtual_address || rva >= section.virtual_address + span) {
            continue;
        }
        const std::uint32_t offset = rva - section.virtual_address;
        if (section.raw_pointer > image_size || section.raw_size <= offset) {
            return false;
        }
        const std::size_t avail = section.raw_size - offset;
        written = (std::min)(out_len, avail);
        if (written == 0) {
            return false;
        }
        std::memcpy(out, image + section.raw_pointer + offset, written);
        return true;
    }
    return false;
}

const char* section_name_for_rva(const std::vector<PeSectionView>& sections, std::uint32_t rva) {
    for (const PeSectionView& section : sections) {
        const std::uint32_t span = section.virtual_size != 0 ? section.virtual_size : section.raw_size;
        if (span != 0 && rva >= section.virtual_address && rva < section.virtual_address + span) {
            return section.name;
        }
    }
    return "?";
}

bool parse_pdata_ranges(const std::uint8_t* image,
                        std::size_t image_size,
                        const std::vector<PeSectionView>& sections,
                        std::vector<RuntimeFunctionRange>& out) {
    out.clear();
    const PeSectionView* pdata = nullptr;
    for (const PeSectionView& section : sections) {
        if (std::strncmp(section.name, ".pdata", 6) == 0) {
            pdata = &section;
            break;
        }
    }
    if (pdata == nullptr || pdata->raw_size < 12) {
        return false;
    }
    if (pdata->raw_pointer > image_size || pdata->raw_size > image_size - pdata->raw_pointer) {
        return false;
    }
    const std::uint8_t* blob = image + pdata->raw_pointer;
    const std::size_t count = pdata->raw_size / 12;
    for (std::size_t i = 0; i < count; ++i) {
        RuntimeFunctionRange range{};
        if (!read_u32_local(blob, pdata->raw_size, i * 12, range.begin)
            || !read_u32_local(blob, pdata->raw_size, i * 12 + 4, range.end)) {
            continue;
        }
        if (range.begin == 0 || range.end <= range.begin) {
            continue;
        }
        out.push_back(range);
    }
    std::sort(out.begin(), out.end(), [](const RuntimeFunctionRange& a, const RuntimeFunctionRange& b) {
        return a.begin < b.begin;
    });
    return out.size() >= 100;
}

bool lookup_pdata_function(const std::vector<RuntimeFunctionRange>& ranges,
                           std::uint32_t rva,
                           RuntimeFunctionRange& out) noexcept {
    if (ranges.empty()) {
        return false;
    }
    std::size_t lo = 0;
    std::size_t hi = ranges.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (ranges[mid].begin <= rva) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == 0) {
        return false;
    }
    const RuntimeFunctionRange& candidate = ranges[lo - 1];
    if (rva >= candidate.begin && rva < candidate.end) {
        out = candidate;
        return true;
    }
    return false;
}

std::uint32_t bounded_instr_len(const std::uint8_t* bytes, std::size_t available, std::size_t offset) noexcept {
    if (offset >= available) {
        return 0;
    }
    std::size_t pos = offset;
    if (bytes[pos] >= 0x40 && bytes[pos] <= 0x4F) {
        ++pos;
        if (pos >= available) {
            return 0;
        }
    }
    const std::uint8_t opcode = bytes[pos++];
    if (opcode != 0x8B && opcode != 0x8D && opcode != 0x89 && opcode != 0xFF) {
        return 0;
    }
    if (pos >= available) {
        return 0;
    }
    const std::uint8_t modrm = bytes[pos++];
    const unsigned mod = (modrm >> 6) & 3u;
    const unsigned rm = modrm & 7u;
    if (mod == 3u) {
        return static_cast<std::uint32_t>(pos - offset);
    }
    if (rm == 4u) {
        ++pos;
        if (pos >= available) {
            return 0;
        }
    }
    if (rm == 5u && mod == 0u) {
        if (pos + 4 > available) {
            return 0;
        }
        pos += 4;
    } else if (mod == 1u) {
        if (pos + 1 > available) {
            return 0;
        }
        ++pos;
    } else if (mod == 2u) {
        if (pos + 4 > available) {
            return 0;
        }
        pos += 4;
    }
    return static_cast<std::uint32_t>(pos - offset);
}

void reg_name(unsigned index, char out[8]) noexcept {
    std::memset(out, 0, 8);
    if (index >= 16) {
        out[0] = '?';
        return;
    }
    std::memcpy(out, kRegNames[index], 7);
}

bool decode_rip_global(const std::uint8_t* bytes,
                       std::size_t available,
                       std::uint32_t instruction_rva,
                       std::uint32_t& global_rva,
                       SemanticFieldAccessType& access,
                       char base_reg[8]) {
    global_rva = 0;
    access = SemanticFieldAccessType::Read;
    reg_name(16, base_reg);

    std::size_t pos = 0;
    if (bytes[0] >= 0x40 && bytes[0] <= 0x4F) {
        ++pos;
    }
    if (pos + 2 > available) {
        return false;
    }
    const std::uint8_t opcode = bytes[pos];
    if (opcode == 0x8B) {
        access = SemanticFieldAccessType::Read;
    } else if (opcode == 0x89) {
        access = SemanticFieldAccessType::Write;
    } else if (opcode == 0x8D) {
        access = SemanticFieldAccessType::AddressCalculation;
    } else {
        return false;
    }
    const std::uint8_t modrm = bytes[pos + 1];
    if ((modrm & 0xC7u) != 0x05u) {
        return false;
    }
    unsigned dest = ((modrm >> 3) & 7u);
    if (pos > 0) {
        dest += 8u;
    }
    reg_name(dest, base_reg);
    const std::uint32_t len = bounded_instr_len(bytes, available, 0);
    if (len < 7) {
        return false;
    }
    global_rva = decode_x64_rip_relative(instruction_rva, bytes, static_cast<std::uint32_t>(pos + 2), len);
    return global_rva != 0;
}

bool decode_field_offset(const std::uint8_t* bytes,
                         std::size_t available,
                         std::uint32_t instruction_rva,
                         SemanticFieldOffsetRecord& out) {
    std::size_t pos = 0;
    if (bytes[0] >= 0x40 && bytes[0] <= 0x4F) {
        ++pos;
    }
    if (pos + 2 > available) {
        return false;
    }
    const std::uint8_t opcode = bytes[pos];
    if (opcode == 0x8B) {
        out.access = SemanticFieldAccessType::Read;
    } else if (opcode == 0x89) {
        out.access = SemanticFieldAccessType::Write;
    } else if (opcode == 0x8D) {
        out.access = SemanticFieldAccessType::AddressCalculation;
    } else {
        return false;
    }
    const std::uint8_t modrm = bytes[pos + 1];
    const unsigned mod = (modrm >> 6) & 3u;
    if (mod == 0u || mod == 3u) {
        return false;
    }
    unsigned base = modrm & 7u;
    if (pos > 0 && base < 8) {
        base += 8u;
    }
    reg_name(base, out.base_register);
    std::size_t disp_pos = pos + 2;
    if ((modrm & 7u) == 4u) {
        disp_pos += 1;
    }
    std::int32_t disp = 0;
    if (mod == 1u) {
        if (disp_pos + 1 > available) {
            return false;
        }
        disp = static_cast<std::int8_t>(bytes[disp_pos]);
    } else if (mod == 2u) {
        if (disp_pos + 4 > available) {
            return false;
        }
        std::memcpy(&disp, bytes + disp_pos, 4);
    } else {
        return false;
    }
    if (disp <= 0) {
        return false;
    }
    out.instruction_rva = instruction_rva;
    out.candidate_offset = static_cast<std::uint32_t>(disp);
    out.competing_interpretations =
        "Offset shape alone does not prove UE field identity; may be unrelated struct member.";
    return true;
}

SemanticEvidenceClass classify_function(const std::vector<std::string>& anchors) {
    std::unordered_set<std::string> primary;
    for (const std::string& anchor : anchors) {
        if (anchor == "PersistentLevel") {
            continue;
        }
        primary.insert(anchor);
    }
    if (primary.count("AcknowledgedPawn") && (primary.count("LocalPlayer") || primary.count("APlayerController"))) {
        return SemanticEvidenceClass::Strong;
    }
    if (primary.size() >= 2) {
        return SemanticEvidenceClass::PromisingUnverified;
    }
    if (primary.size() == 1) {
        return SemanticEvidenceClass::Weak;
    }
    return SemanticEvidenceClass::Weak;
}

bool sha_matches(std::string_view sha256_hex) {
    if (sha256_hex.size() != 64) {
        return false;
    }
    return sha256_hex == kLocalPlayerSemanticTargetSha256;
}

} // namespace

const char* to_string(SemanticCandidateKind kind) noexcept {
    switch (kind) {
    case SemanticCandidateKind::Function:
        return "SEMANTIC_FUNCTION_CANDIDATE";
    case SemanticCandidateKind::Global:
        return "SEMANTIC_GLOBAL_CANDIDATE";
    case SemanticCandidateKind::FieldOffset:
        return "FIELD_OFFSET_CANDIDATE";
    case SemanticCandidateKind::Relationship:
        return "RELATIONSHIP_CANDIDATE";
    }
    return "UNKNOWN";
}

const char* to_string(SemanticEvidenceClass classification) noexcept {
    switch (classification) {
    case SemanticEvidenceClass::Strong:
        return "SEMANTIC_EVIDENCE_STRONG";
    case SemanticEvidenceClass::PromisingUnverified:
        return "SEMANTIC_EVIDENCE_PROMISING_UNVERIFIED";
    case SemanticEvidenceClass::Weak:
        return "SEMANTIC_EVIDENCE_WEAK";
    case SemanticEvidenceClass::Contradicted:
        return "SEMANTIC_EVIDENCE_CONTRADICTED";
    }
    return "UNKNOWN";
}

const char* to_string(SemanticFieldAccessType access) noexcept {
    switch (access) {
    case SemanticFieldAccessType::Read:
        return "read";
    case SemanticFieldAccessType::Write:
        return "write";
    case SemanticFieldAccessType::AddressCalculation:
        return "address_calculation";
    }
    return "unknown";
}

std::vector<SemanticAnchorSpec> semantic_anchor_catalog() noexcept {
    return {
        {"AcknowledgedPawn", 1, false},
        {"LocalPlayer", 2, false},
        {"APlayerController", 3, false},
        {"PlayerController", 4, false},
        {"OwningGameInstance", 5, false},
        {"PersistentLevel", 6, true},
    };
}

SemanticAnalysisSummary analyze_local_player_semantic_image(const std::uint8_t* image,
                                                            std::size_t image_size,
                                                            std::string_view expected_sha256_hex) {
    SemanticAnalysisSummary summary{};
    summary.image_size = image_size;

    if (image == nullptr || image_size < 0x1000) {
        summary.message = "Image buffer is too small for semantic analysis.";
        return summary;
    }

    PeImageInfo pe{};
    if (parse_pe64_headers(image, image_size, pe) != PeParseStatus::Ok) {
        summary.message = "Failed to parse PE headers.";
        return summary;
    }

    std::string computed_sha;
    if (detail::sha256_buffer(image, image_size, computed_sha)) {
        summary.sha_verified = expected_sha256_hex.empty() || computed_sha == expected_sha256_hex;
    }

    std::vector<PeSectionView> sections;
    if (!pe_enumerate_sections(image, image_size, sections)) {
        summary.message = "Failed to enumerate PE sections.";
        return summary;
    }

    std::vector<RuntimeFunctionRange> pdata_ranges;
    const bool pdata_usable = parse_pdata_ranges(image, image_size, sections, pdata_ranges);

    const auto anchors = semantic_anchor_catalog();
    std::unordered_map<std::uint32_t, std::string> string_rva_to_anchor;
    std::unordered_map<std::string, std::vector<SemanticStringLocation>> strings_by_anchor;

    for (const SemanticAnchorSpec& spec : anchors) {
        for (const PeSectionView& section : sections) {
            if (!section_is_readonly_data(section)) {
                continue;
            }
            if (section.raw_pointer > image_size || section.raw_size > image_size - section.raw_pointer) {
                continue;
            }
            const std::uint8_t* blob = image + section.raw_pointer;
            const std::size_t blob_size = section.raw_size;
            const std::size_t nlen = std::strlen(spec.text);
            for (std::size_t i = 0; i + nlen <= blob_size; ++i) {
                if (std::memcmp(blob + i, spec.text, nlen) != 0) {
                    continue;
                }
                SemanticStringLocation loc{};
                loc.anchor = spec.text;
                loc.string_rva = section.virtual_address + static_cast<std::uint32_t>(i);
                std::memcpy(loc.section, section.name, 8);
                loc.section[8] = '\0';
                strings_by_anchor[spec.text].push_back(loc);
                string_rva_to_anchor[loc.string_rva] = spec.text;
            }
        }
    }

    for (const SemanticAnchorSpec& spec : anchors) {
        SemanticAnchorAnalysis anchor_analysis{};
        anchor_analysis.anchor = spec.text;
        anchor_analysis.strings = strings_by_anchor[spec.text];
        anchor_analysis.string_occurrences = anchor_analysis.strings.size();

        for (const PeSectionView& section : sections) {
            if (!section.executable) {
                continue;
            }
            if (section.raw_pointer > image_size || section.raw_size > image_size - section.raw_pointer) {
                continue;
            }
            const std::uint8_t* blob = image + section.raw_pointer;
            const std::size_t blob_size = section.raw_size;
            for (std::size_t i = 0; i + 7 <= blob_size; ++i) {
                if (anchor_analysis.xrefs.size() >= kSemanticMaxDirectXrefs) {
                    anchor_analysis.xrefs_truncated = true;
                    break;
                }
                std::uint32_t target = 0;
                const std::uint32_t instr_rva =
                    section.virtual_address + static_cast<std::uint32_t>(i);
                if (!decode_x64_rip_relative_lea_or_mov(blob + i,
                                                        blob_size - i,
                                                        instr_rva,
                                                        target)) {
                    continue;
                }
                const auto it = string_rva_to_anchor.find(target);
                if (it == string_rva_to_anchor.end() || it->second != spec.text) {
                    continue;
                }
                SemanticDirectXref xref{};
                xref.anchor = spec.text;
                xref.instruction_rva = instr_rva;
                xref.string_rva = target;
                std::memcpy(xref.instruction_bytes, blob + i, 7);
                anchor_analysis.xrefs.push_back(xref);
            }
            if (anchor_analysis.xrefs_truncated) {
                break;
            }
        }
        anchor_analysis.direct_xrefs = anchor_analysis.xrefs.size();

        std::unordered_map<std::uint32_t, SemanticFunctionRecord> functions;
        for (const SemanticDirectXref& xref : anchor_analysis.xrefs) {
            if (!pdata_usable) {
                continue;
            }
            RuntimeFunctionRange fn{};
            if (!lookup_pdata_function(pdata_ranges, xref.instruction_rva, fn)) {
                continue;
            }
            if (functions.size() >= kSemanticMaxPdataFunctions && functions.count(fn.begin) == 0) {
                anchor_analysis.functions_truncated = true;
                continue;
            }
            SemanticFunctionRecord& record = functions[fn.begin];
            if (record.begin_rva == 0) {
                record.begin_rva = fn.begin;
                record.end_rva = fn.end;
                record.provenance = "SEMANTIC_FUNCTION_CANDIDATE from .pdata + direct string xref";
            }
            record.xref_instruction_rvas.push_back(xref.instruction_rva);
            if (std::find(record.anchors_present.begin(), record.anchors_present.end(), spec.text)
                == record.anchors_present.end()) {
                record.anchors_present.push_back(spec.text);
            }
        }

        for (auto& entry : functions) {
            SemanticFunctionRecord& record = entry.second;
            const std::uint32_t span = record.end_rva > record.begin_rva
                ? record.end_rva - record.begin_rva
                : 0;
            record.window_bytes = static_cast<std::uint32_t>(
                (std::min)(static_cast<std::size_t>(span), kSemanticMaxFunctionWindow));

            std::vector<std::uint8_t> window(record.window_bytes);
            std::size_t written = 0;
            if (!read_bytes_at_rva(image,
                                   image_size,
                                   sections,
                                   record.begin_rva,
                                   window.data(),
                                   window.size(),
                                   written)
                || written < 7) {
                continue;
            }

            std::unordered_set<std::uint32_t> global_rvas_seen;
            for (std::size_t i = 0; i + 7 <= written;) {
                {
                    std::uint32_t target = 0;
                    if (decode_x64_rip_relative_lea_or_mov(window.data() + i,
                                                           written - i,
                                                           record.begin_rva + static_cast<std::uint32_t>(i),
                                                           target)) {
                        const auto sit = string_rva_to_anchor.find(target);
                        if (sit != string_rva_to_anchor.end()
                            && std::find(record.anchors_present.begin(),
                                         record.anchors_present.end(),
                                         sit->second)
                                == record.anchors_present.end()) {
                            record.anchors_present.push_back(sit->second);
                        }
                    }
                }

                if (anchor_analysis.globals.size() < kSemanticMaxGlobalRefsPerAnchor) {
                    std::uint32_t global_rva = 0;
                    SemanticFieldAccessType access = SemanticFieldAccessType::Read;
                    char base_reg[8]{};
                    if (decode_rip_global(window.data() + i,
                                          written - i,
                                          record.begin_rva + static_cast<std::uint32_t>(i),
                                          global_rva,
                                          access,
                                          base_reg)
                        && global_rvas_seen.insert(global_rva).second) {
                        SemanticGlobalRecord global{};
                        global.global_rva = global_rva;
                        const char* gsec = section_name_for_rva(sections, global_rva);
                        std::memcpy(global.section, gsec, 8);
                        global.section[8] = '\0';
                        global.access = access;
                        global.referencing_function_rvas.push_back(record.begin_rva);
                        global.associated_anchors = record.anchors_present;
                        global.provenance =
                            "SEMANTIC_GLOBAL_CANDIDATE from bounded RIP-relative access in anchor function";
                        global.classification = SemanticEvidenceClass::PromisingUnverified;
                        anchor_analysis.globals.push_back(global);
                    }
                } else {
                    anchor_analysis.global_refs_truncated = true;
                }

                SemanticFieldOffsetRecord field{};
                field.function_begin_rva = record.begin_rva;
                field.anchor_relationship = spec.text;
                if (decode_field_offset(window.data() + i,
                                        written - i,
                                        record.begin_rva + static_cast<std::uint32_t>(i),
                                        field)) {
                    field.evidence = "Static disp32 object-relative access in semantic function window";
                    field.classification = SemanticEvidenceClass::PromisingUnverified;
                    anchor_analysis.field_offsets.push_back(field);
                }

                const std::uint32_t adv = bounded_instr_len(window.data(), written, i);
                i += adv > 0 ? adv : 1;
            }

            record.classification = classify_function(record.anchors_present);
            switch (record.classification) {
            case SemanticEvidenceClass::Strong:
                record.classification_rationale =
                    "Multiple priority semantic anchors referenced within one .pdata function.";
                break;
            case SemanticEvidenceClass::PromisingUnverified:
                record.classification_rationale =
                    "Multiple anchors or corroborating globals/offsets; identity not proven.";
                break;
            default:
                record.classification_rationale =
                    "Single-anchor or sparse semantic reference; weak offline correlation.";
                break;
            }
        }

        for (const auto& entry : functions) {
            anchor_analysis.functions.push_back(entry.second);
        }
        std::sort(anchor_analysis.functions.begin(),
                  anchor_analysis.functions.end(),
                  [](const SemanticFunctionRecord& a, const SemanticFunctionRecord& b) {
                      return a.begin_rva < b.begin_rva;
                  });
        anchor_analysis.pdata_functions = anchor_analysis.functions.size();

        if (anchor_analysis.xrefs_truncated || anchor_analysis.functions_truncated
            || anchor_analysis.global_refs_truncated) {
            summary.bounds.any_truncation = true;
            summary.bounds.truncation_notes.push_back(std::string(spec.text) + " hit analysis bounds.");
        }

        summary.anchors.push_back(std::move(anchor_analysis));
    }

    std::unordered_map<std::uint32_t, SemanticRelationshipRecord> relationships;
    for (const SemanticAnchorAnalysis& anchor_analysis : summary.anchors) {
        for (const SemanticFunctionRecord& fn : anchor_analysis.functions) {
            if (fn.anchors_present.size() < 2) {
                continue;
            }
            SemanticRelationshipRecord& rel = relationships[fn.begin_rva];
            rel.function_begin_rva = fn.begin_rva;
            rel.anchors = fn.anchors_present;
            rel.description =
                "RELATIONSHIP_CANDIDATE: multiple semantic anchors in one .pdata function";
            rel.classification = classify_function(fn.anchors_present);
            rel.provenance = "Shared-function anchor correlation";
        }
    }
    for (const auto& entry : relationships) {
        summary.relationships.push_back(entry.second);
    }

    bool strong_relationship = false;
    for (const SemanticRelationshipRecord& rel : summary.relationships) {
        if (rel.classification == SemanticEvidenceClass::Strong
            || rel.classification == SemanticEvidenceClass::PromisingUnverified) {
            strong_relationship = true;
            break;
        }
    }

    summary.justified_live_candidate = strong_relationship;
    if (summary.justified_live_candidate) {
        summary.live_candidate_rationale =
            "Offline correlation found at least one .pdata function referencing multiple priority "
            "player-related semantic anchors; warrants a bounded read-only live falsification experiment.";
        summary.recommended_live_experiment =
            "Task #8F (proposed): For the highest-priority multi-anchor function, read-only observe "
            "RIP-relative globals referenced within that function's static window across manual phases. "
            "Maximum pointer depth 2, 8 bytes per hop, 64 bytes per object prefix. Process rights "
            "QUERY_LIMITED|VM_READ only. Falsify if globals are null/non-canonical during POST_FRONTEND "
            "gameplay or if values do not correlate with manual phase labels. Maximum conclusion: "
            "LOCAL_PLAYER_CANDIDATE_PRESERVED — never LOCAL_PLAYER_IDENTIFIED.";
    } else {
        summary.live_candidate_rationale =
            "No function exhibited sufficiently direct multi-anchor semantic correlation to justify "
            "a live identity experiment without guessing.";
        summary.recommended_live_experiment = "NO JUSTIFIED LIVE CANDIDATE";
    }

    summary.message = summary.sha_verified
        ? "Offline local-player semantic correlation completed."
        : "Semantic analysis completed but SHA-256 did not match expected build.";
    return summary;
}

SemanticAnalysisSummary run_local_player_semantic_research_file(std::wstring_view path,
                                                                std::string_view expected_sha256_hex) {
    SemanticAnalysisSummary summary{};
    if (path.empty()) {
        summary.message = "Executable path is empty.";
        return summary;
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
        summary.message = "Could not open executable for semantic research.";
        return summary;
    }

    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
        CloseHandle(file);
        summary.message = "Could not map executable for semantic research.";
        return summary;
    }

    const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (view == nullptr) {
        CloseHandle(mapping);
        CloseHandle(file);
        summary.message = "Could not map view of executable.";
        return summary;
    }

    LARGE_INTEGER file_size{};
    if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart <= 0) {
        UnmapViewOfFile(view);
        CloseHandle(mapping);
        CloseHandle(file);
        summary.message = "Executable size is invalid.";
        return summary;
    }

    summary = analyze_local_player_semantic_image(
        static_cast<const std::uint8_t*>(view),
        static_cast<std::size_t>(file_size.QuadPart),
        expected_sha256_hex);

    UnmapViewOfFile(view);
    CloseHandle(mapping);
    CloseHandle(file);
    return summary;
}

namespace {

std::string hex_rva(std::uint32_t rva) {
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "0x%08X", rva);
    return buf;
}

} // namespace

std::vector<std::string> format_local_player_semantic_report(const SemanticAnalysisSummary& summary) {
    std::vector<std::string> lines;
    lines.push_back("Local Player Semantic Research (Task #8E)");
    lines.push_back("SHA verified: " + std::string(summary.sha_verified ? "yes" : "no"));
    lines.push_back("Image size: " + std::to_string(summary.image_size));
    lines.push_back(summary.message);
    if (summary.bounds.any_truncation) {
        lines.push_back("Truncation: yes");
        for (const std::string& note : summary.bounds.truncation_notes) {
            lines.push_back("  " + note);
        }
    }
    for (const SemanticAnchorAnalysis& anchor : summary.anchors) {
        lines.push_back(std::string("Anchor: ") + anchor.anchor);
        lines.push_back("  strings=" + std::to_string(anchor.string_occurrences)
                        + " xrefs=" + std::to_string(anchor.direct_xrefs)
                        + " functions=" + std::to_string(anchor.pdata_functions)
                        + (anchor.xrefs_truncated ? " XREFS_TRUNCATED" : "")
                        + (anchor.functions_truncated ? " FUNCTIONS_TRUNCATED" : "")
                        + (anchor.global_refs_truncated ? " GLOBALS_TRUNCATED" : ""));
        for (const SemanticStringLocation& loc : anchor.strings) {
            lines.push_back("  string_rva=" + hex_rva(loc.string_rva) + " section=" + loc.section);
        }
        for (const SemanticDirectXref& xref : anchor.xrefs) {
            lines.push_back("  xref instr=" + hex_rva(xref.instruction_rva) + " string="
                            + hex_rva(xref.string_rva));
        }
        for (const SemanticFunctionRecord& fn : anchor.functions) {
            std::string anchor_list;
            for (std::size_t i = 0; i < fn.anchors_present.size(); ++i) {
                if (i > 0) {
                    anchor_list += ",";
                }
                anchor_list += fn.anchors_present[i];
            }
            lines.push_back("  fn=" + hex_rva(fn.begin_rva) + " class=" + to_string(fn.classification)
                            + " anchors=[" + anchor_list + "]");
        }
        for (const SemanticGlobalRecord& global : anchor.globals) {
            lines.push_back("  global=" + hex_rva(global.global_rva) + " section=" + global.section
                            + " access=" + to_string(global.access));
        }
        for (const SemanticFieldOffsetRecord& field : anchor.field_offsets) {
            lines.push_back("  field fn=" + hex_rva(field.function_begin_rva) + " instr="
                            + hex_rva(field.instruction_rva) + " off=0x"
                            + hex_rva(field.candidate_offset).substr(2) + " access="
                            + to_string(field.access) + " reg=" + field.base_register);
        }
    }
    for (const SemanticRelationshipRecord& rel : summary.relationships) {
        std::string anchor_list;
        for (std::size_t i = 0; i < rel.anchors.size(); ++i) {
            if (i > 0) {
                anchor_list += ",";
            }
            anchor_list += rel.anchors[i];
        }
        lines.push_back("Relationship fn=" + hex_rva(rel.function_begin_rva) + " class="
                        + to_string(rel.classification) + " anchors=[" + anchor_list + "]");
    }
    lines.push_back("Justified live candidate: " + std::string(summary.justified_live_candidate ? "yes" : "no"));
    lines.push_back(summary.live_candidate_rationale);
    lines.push_back(summary.recommended_live_experiment);
    return lines;
}

} // namespace deflux::game::gta_sa_de
