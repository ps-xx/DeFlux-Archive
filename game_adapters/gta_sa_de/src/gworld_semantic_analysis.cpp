#include "deflux/game/gta_sa_de/gworld_semantic_analysis.hpp"

#include "deflux/game/gta_sa_de/gworld_discovery_research.hpp"
#include "gworld_discovery.hpp"
#include "pe_image.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace deflux::game::gta_sa_de {

namespace {

constexpr std::size_t kPostLoadWindow = 48;
constexpr std::size_t kPreLoadWindow = 8;
constexpr std::size_t kCodeRegionBucket = 4096;

struct RuntimeFunctionRange {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

const char* kRegNames[] = {
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
};

bool read_u32_local(const std::uint8_t* data, std::size_t size, std::size_t offset, std::uint32_t& out) noexcept {
    if (data == nullptr || offset + 4 > size) {
        return false;
    }
    out = static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24);
    return true;
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

bool modrm_memory_uses_reg(std::uint8_t modrm, unsigned reg_index, bool has_sib, std::uint8_t sib) noexcept {
    const unsigned mod = (modrm >> 6) & 3u;
    const unsigned rm = modrm & 7u;
    if (mod == 3u) {
        return false;
    }
    if (rm == 4u && has_sib) {
        return (sib & 7u) == reg_index;
    }
    if (rm == 5u && mod == 0u) {
        return false;
    }
    return rm == reg_index;
}

bool reg_index_from_modrm_reg(std::uint8_t modrm, bool rex_r, unsigned& out) noexcept {
    if (((modrm >> 6) & 3u) == 3u) {
        out = ((modrm >> 3) & 7u) + (rex_r ? 8u : 0u);
        return true;
    }
    return false;
}

void copy_reg_name(unsigned index, char out[8]) noexcept {
    std::memset(out, 0, 8);
    if (index >= 16) {
        out[0] = '?';
        return;
    }
    std::memcpy(out, kRegNames[index], std::min<std::size_t>(7, std::strlen(kRegNames[index])));
}

bool parse_pdata_ranges(const std::uint8_t* image,
                        std::size_t image_size,
                        const std::vector<PeSectionView>& sections,
                        std::vector<RuntimeFunctionRange>& out,
                        GWorldPdataCoverage& coverage) {
    out.clear();
    coverage = {};
    coverage.parsed = true;

    const PeSectionView* pdata = nullptr;
    for (const PeSectionView& section : sections) {
        if (std::strncmp(section.name, ".pdata", 6) == 0) {
            pdata = &section;
            break;
        }
    }
    if (pdata == nullptr || pdata->raw_size < 12) {
        coverage.message = ".pdata section not present or too small.";
        return false;
    }
    if (pdata->raw_pointer > image_size || pdata->raw_size > image_size - pdata->raw_pointer) {
        coverage.message = ".pdata section raw range out of bounds.";
        return false;
    }

    const std::uint8_t* blob = image + pdata->raw_pointer;
    const std::size_t count = pdata->raw_size / 12;
    out.reserve(count);
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
    coverage.runtime_function_count = out.size();
    coverage.usable = out.size() >= 100;
    coverage.message = coverage.usable
        ? "Conservative .pdata RUNTIME_FUNCTION table parsed."
        : "Parsed .pdata but table too small for reliable function mapping.";
    return coverage.usable;
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
        written = std::min(out_len, avail);
        if (written == 0) {
            return false;
        }
        std::memcpy(out, image + section.raw_pointer + offset, written);
        return true;
    }
    return false;
}

char section_name_for_rva(const std::vector<PeSectionSummary>& sections, std::uint32_t rva) {
    for (const PeSectionSummary& section : sections) {
        if (section.virtual_size != 0 && rva >= section.virtual_address
            && rva < section.virtual_address + section.virtual_size) {
            return section.name[0] != '\0' ? section.name[0] : '?';
        }
    }
    return '?';
}

GWorldReferenceSemanticDetail analyze_reference_semantics(
    const std::uint8_t* image,
    std::size_t image_size,
    const std::vector<PeSectionView>& sections,
    const std::vector<PeSectionSummary>& summaries,
    const std::vector<RuntimeFunctionRange>& pdata_ranges,
    bool pdata_usable,
    std::uint32_t instruction_rva,
    std::uint32_t slot_rva) {
    GWorldReferenceSemanticDetail detail{};
    detail.instruction_rva = instruction_rva;
    detail.code_region_id = instruction_rva / static_cast<std::uint32_t>(kCodeRegionBucket);

    for (const PeSectionSummary& section : summaries) {
        if (section.virtual_size != 0 && instruction_rva >= section.virtual_address
            && instruction_rva < section.virtual_address + section.virtual_size) {
            std::memcpy(detail.source_section, section.name, 8);
            detail.source_section[8] = '\0';
            break;
        }
    }
    (void)section_name_for_rva(summaries, instruction_rva);

    std::array<std::uint8_t, 7 + kPostLoadWindow> window{};
    std::size_t written = 0;
    if (!read_bytes_at_rva(image, image_size, sections, instruction_rva, window.data(), window.size(), written)
        || written < 7) {
        detail.usage.analysis_truncated = true;
        return detail;
    }

    std::memcpy(detail.instruction_bytes, window.data(), 7);
    detail.loaded_reg_index = decode_mov_rip_dest_reg_index(window.data(), written);
    copy_reg_name(detail.loaded_reg_index, detail.dest_reg);

    const std::size_t pre_len = std::min<std::size_t>(instruction_rva, kPreLoadWindow);
    if (pre_len > 0) {
        std::size_t pre_written = 0;
        const std::uint32_t pre_rva = instruction_rva - static_cast<std::uint32_t>(pre_len);
        (void)read_bytes_at_rva(
            image, image_size, sections, pre_rva, detail.pre_context_bytes, pre_len, pre_written);
    }

    const std::size_t post_off = 7;
    const std::size_t post_len = written > post_off ? written - post_off : 0;
    if (post_len > 0) {
        std::memcpy(detail.post_context_bytes,
                    window.data() + post_off,
                    std::min(post_len, sizeof(detail.post_context_bytes)));
        detail.usage = analyze_bounded_post_load_usage(window.data() + post_off, post_len, detail.loaded_reg_index);
    } else {
        detail.usage.analysis_truncated = true;
    }

    RuntimeFunctionRange fn{};
    if (pdata_usable && lookup_pdata_function(pdata_ranges, instruction_rva, fn)) {
        detail.boundary_source = GWorldFunctionBoundarySource::Pdata;
        detail.function_begin_rva = fn.begin;
        detail.function_end_rva = fn.end;
    } else {
        detail.boundary_source = GWorldFunctionBoundarySource::CodeRegionHeuristic;
        detail.function_begin_rva = detail.code_region_id * static_cast<std::uint32_t>(kCodeRegionBucket);
        detail.function_end_rva = detail.function_begin_rva + static_cast<std::uint32_t>(kCodeRegionBucket);
    }
    (void)slot_rva;
    return detail;
}

GWorldSemanticCandidateRecord build_semantic_record(
    std::uint32_t slot_rva,
    const char dest_section[9],
    const std::vector<GWorldReferenceSemanticDetail>& refs,
    std::size_t total_recognized,
    bool convergence_member,
    bool pdata_usable) {
    GWorldSemanticCandidateRecord record{};
    record.slot_rva = slot_rva;
    std::memcpy(record.dest_section, dest_section, 9);
    record.convergence_dataset_member = convergence_member;
    record.convergence_note = convergence_member
        ? "Included via HEURISTIC convergence threshold (>=12 context-qualified references)."
        : "Below HEURISTIC convergence threshold; included for comparison only.";

    record.structural.in_image = true;
    record.structural.in_pointer_storage_section = true;
    record.structural.aligned_8 = (slot_rva & 7u) == 0;
    record.structural.non_iat = true;

    record.diversity.total_recognized_references = total_recognized;
    record.diversity.context_qualified_references = refs.size();
    record.diversity.pdata_boundaries_available = pdata_usable;

    std::unordered_set<std::uint32_t> unique_instr;
    std::unordered_set<std::uint32_t> unique_regions;
    std::unordered_set<std::uint32_t> unique_functions;
    std::unordered_set<unsigned> dest_regs;
    std::unordered_map<std::uint32_t, std::size_t> region_counts;

    for (const GWorldReferenceSemanticDetail& ref : refs) {
        unique_instr.insert(ref.instruction_rva);
        unique_regions.insert(ref.code_region_id);
        region_counts[ref.code_region_id]++;
        if (ref.boundary_source == GWorldFunctionBoundarySource::Pdata) {
            unique_functions.insert(ref.function_begin_rva);
        }
        if (ref.loaded_reg_index < 16) {
            dest_regs.insert(ref.loaded_reg_index);
        }
        if (ref.usage.null_test) {
            ++record.control_flow.refs_with_null_test;
        }
        if (ref.usage.null_branch) {
            ++record.control_flow.refs_with_null_branch;
        }
        if (ref.usage.post_load_dereference) {
            ++record.object_usage.refs_with_post_load_dereference;
        }
        if (ref.usage.field_access) {
            ++record.object_usage.refs_with_field_access;
        }
        if (ref.usage.indirect_vtable_like) {
            ++record.object_usage.refs_with_vtable_like;
        }
        if (ref.usage.register_propagation) {
            ++record.object_usage.refs_with_register_propagation;
        }
        if (ref.usage.post_load_dereference || ref.usage.field_access || ref.usage.indirect_vtable_like) {
            ++record.object_usage.refs_with_any_object_usage;
        } else {
            ++record.negative.refs_without_object_usage;
        }
        if (ref.usage.scalar_like_usage) {
            ++record.negative.refs_with_scalar_like_usage;
        }
        if (ref.usage.table_index_like) {
            ++record.negative.refs_with_table_index_like;
        }
    }

    record.diversity.unique_instruction_rvas = unique_instr.size();
    record.diversity.unique_code_regions = unique_regions.size();
    record.diversity.unique_pdata_functions = unique_functions.size();
    if (!region_counts.empty()) {
        std::size_t max_region = 0;
        for (const auto& entry : region_counts) {
            max_region = std::max(max_region, entry.second);
        }
        record.diversity.references_concentrated_in_one_region =
            unique_regions.size() == 1 && refs.size() >= 5 && max_region == refs.size();
    }

    record.negative.scalar_dominant =
        !refs.empty() && record.negative.refs_with_scalar_like_usage * 2 >= refs.size();

    record.control_flow.repeated_load_test_branch_pattern =
        record.control_flow.refs_with_null_test > 0 && record.control_flow.refs_with_null_branch > 0
        && record.control_flow.refs_with_null_test * 10 >= refs.size() * 8;

    for (unsigned reg : dest_regs) {
        record.dest_registers.push_back(kRegNames[reg]);
    }
    std::sort(record.dest_registers.begin(), record.dest_registers.end());

    record.reference_details = refs;
    record.classification = classify_semantic_candidate(
        record.structural,
        record.diversity,
        record.object_usage,
        record.control_flow,
        record.negative,
        record.classification_rationale);
    return record;
}

} // namespace

unsigned decode_mov_rip_dest_reg_index(const std::uint8_t* bytes, std::size_t available) noexcept {
    if (bytes == nullptr || available < 3 || (bytes[0] != 0x48 && bytes[0] != 0x4C) || bytes[1] != 0x8B) {
        return 16;
    }
    const bool rex_r = (bytes[0] & 0x04u) != 0;
    const std::uint8_t modrm = bytes[2];
    if ((modrm & 0xC7u) != 0x05u) {
        return 16;
    }
    return ((modrm >> 3) & 7u) + (rex_r ? 8u : 0u);
}

GWorldPostLoadUsageEvidence analyze_bounded_post_load_usage(const std::uint8_t* bytes,
                                                            std::size_t available,
                                                            unsigned loaded_reg_index) noexcept {
    GWorldPostLoadUsageEvidence out{};
    if (bytes == nullptr || available == 0 || loaded_reg_index >= 16) {
        out.analysis_truncated = true;
        return out;
    }

    const std::size_t scan_len = std::min(available, static_cast<std::size_t>(48));
    out.null_test = bytes_have_null_test(bytes, scan_len);
    out.null_branch = bytes_have_null_branch(bytes, scan_len);

    bool saw_deref = false;
    for (std::size_t i = 0; i + 3 <= scan_len; ++i) {
        std::size_t pos = i;
        bool rex_w = false;
        bool rex_r = false;
        bool rex_b = false;
        if (bytes[pos] >= 0x40 && bytes[pos] <= 0x4F) {
            rex_w = (bytes[pos] & 0x08u) != 0;
            rex_r = (bytes[pos] & 0x04u) != 0;
            rex_b = (bytes[pos] & 0x01u) != 0;
            ++pos;
            if (pos >= scan_len) {
                continue;
            }
        }
        const std::uint8_t opcode = bytes[pos++];
        if (opcode == 0x85 && pos < scan_len) {
            const std::uint8_t modrm = bytes[pos];
            if (((modrm >> 3) & 7u) + (rex_r ? 8u : 0u) == loaded_reg_index
                && ((modrm & 7u) == ((modrm >> 3) & 7u))) {
                out.null_test = true;
            }
        } else if (opcode == 0x8B && rex_w && pos < scan_len) {
            const std::uint8_t modrm = bytes[pos];
            const bool has_sib = ((modrm >> 6) & 3u) != 3u && (modrm & 7u) == 4u;
            std::uint8_t sib = 0;
            if (has_sib && pos + 1 < scan_len) {
                sib = bytes[pos + 1];
            }
            if (modrm_memory_uses_reg(modrm, loaded_reg_index, has_sib, sib)) {
                const unsigned mod = (modrm >> 6) & 3u;
                if (mod == 0u && (modrm & 7u) == 5u) {
                    continue;
                }
                out.post_load_dereference = true;
                if (mod != 0u) {
                    out.field_access = true;
                }
                saw_deref = true;
            }
        } else if (opcode == 0x89 && pos < scan_len) {
            const std::uint8_t modrm = bytes[pos];
            unsigned src = 0;
            if (reg_index_from_modrm_reg(modrm, rex_r, src) && src == loaded_reg_index) {
                out.register_propagation = true;
            }
        } else if (opcode == 0x83 && pos < scan_len) {
            const std::uint8_t modrm = bytes[pos];
            const unsigned rm = (modrm & 7u) + (rex_b ? 8u : 0u);
            if (((modrm >> 6) & 3u) == 3u && rm == loaded_reg_index) {
                out.scalar_like_usage = true;
            }
        } else if (opcode == 0xFF && pos < scan_len) {
            const std::uint8_t modrm = bytes[pos];
            if (((modrm >> 3) & 7u) == 0u || ((modrm >> 3) & 7u) == 1u) {
                if (modrm_memory_uses_reg(modrm, loaded_reg_index, (modrm & 7u) == 4u, 0)) {
                    out.table_index_like = true;
                }
            }
        }
    }

    if (saw_deref) {
        for (std::size_t i = 0; i + 6 <= scan_len; ++i) {
            if (bytes[i] == 0x48 && bytes[i + 1] == 0x8B) {
                const std::uint8_t modrm = bytes[i + 2];
                if (((modrm >> 6) & 3u) == 0u && (modrm & 7u) != 4u && (modrm & 7u) != 5u) {
                    out.indirect_vtable_like = true;
                    break;
                }
            }
        }
    }

    if (available < 48) {
        out.analysis_truncated = true;
    }
    return out;
}

GWorldSemanticClassification classify_semantic_candidate(
    const GWorldStructuralEvidence& structural,
    const GWorldReferenceDiversityEvidence& diversity,
    const GWorldObjectUsageEvidence& object_usage,
    const GWorldControlFlowEvidence& control_flow,
    const GWorldNegativeEvidence& negative,
    std::string& rationale_out) {
    rationale_out.clear();
    if (!structural.in_image || !structural.in_pointer_storage_section || !structural.aligned_8
        || !structural.non_iat) {
        rationale_out = "Failed structural prerequisites.";
        return GWorldSemanticClassification::Unlikely;
    }

    const bool diversity_ok =
        diversity.unique_code_regions >= 2
        || (diversity.pdata_boundaries_available && diversity.unique_pdata_functions >= 2);
    const bool object_ok = object_usage.refs_with_any_object_usage >= 2;
    const bool scalar_not_dominant = !negative.scalar_dominant;
    const bool not_concentrated = !diversity.references_concentrated_in_one_region
        || object_usage.refs_with_any_object_usage >= 3;

    if (diversity_ok && object_ok && scalar_not_dominant && not_concentrated
        && control_flow.refs_with_null_test > 0 && control_flow.refs_with_null_branch > 0) {
        rationale_out =
            "Object-style post-load usage observed across multiple code regions/functions with "
            "null-test/branch control flow and without scalar-dominant usage.";
        return GWorldSemanticClassification::GWorldLike;
    }

    if (object_usage.refs_with_any_object_usage == 0) {
        rationale_out = "No bounded object-style post-load usage observed.";
        return GWorldSemanticClassification::Unlikely;
    }

    if (negative.scalar_dominant && object_usage.refs_with_any_object_usage <= 1) {
        rationale_out = "Scalar-like post-load usage dominates with insufficient object evidence.";
        return GWorldSemanticClassification::Unlikely;
    }

    rationale_out =
        "Some object-style or control-flow evidence present, but diversity or consistency is "
        "insufficient for a GWorld-like research classification.";
    return GWorldSemanticClassification::Ambiguous;
}

const char* to_string(GWorldSemanticClassification classification) noexcept {
    switch (classification) {
    case GWorldSemanticClassification::GWorldLike:
        return "GWorld-like";
    case GWorldSemanticClassification::Ambiguous:
        return "Ambiguous";
    case GWorldSemanticClassification::Unlikely:
        return "Unlikely";
    }
    return "Unknown";
}

const char* to_string(GWorldFunctionBoundarySource source) noexcept {
    switch (source) {
    case GWorldFunctionBoundarySource::Unknown:
        return "UNKNOWN";
    case GWorldFunctionBoundarySource::Pdata:
        return "PDATA";
    case GWorldFunctionBoundarySource::CodeRegionHeuristic:
        return "CODE_REGION";
    }
    return "Unknown";
}

GWorldSemanticAnalysisBundle analyze_semantic_candidates(
    const std::uint8_t* image,
    std::size_t image_size,
    const std::vector<PeSectionSummary>& sections,
    const std::unordered_map<std::uint32_t, std::size_t>& context_reference_counts,
    const std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>& context_instruction_rvas,
    const std::unordered_map<std::uint32_t, std::array<char, 9>>& slot_dest_sections,
    const std::unordered_map<std::uint32_t, std::size_t>& total_reference_counts,
    std::size_t convergence_threshold) {
    GWorldSemanticAnalysisBundle bundle{};

    std::vector<PeSectionView> raw_sections;
    if (!pe_enumerate_sections(image, image_size, raw_sections)) {
        bundle.summary.message = "Failed to enumerate sections for semantic analysis.";
        return bundle;
    }

    std::vector<RuntimeFunctionRange> pdata_ranges;
    const bool pdata_usable = parse_pdata_ranges(image, image_size, raw_sections, pdata_ranges, bundle.summary.pdata);

    const auto build_for_slot = [&](std::uint32_t slot_rva, bool convergence_member) {
        const auto instr_it = context_instruction_rvas.find(slot_rva);
        if (instr_it == context_instruction_rvas.end()) {
            return;
        }
        char dest_section[9]{};
        const auto dest_it = slot_dest_sections.find(slot_rva);
        if (dest_it != slot_dest_sections.end()) {
            std::memcpy(dest_section, dest_it->second.data(), 9);
        }
        std::vector<GWorldReferenceSemanticDetail> refs;
        refs.reserve(instr_it->second.size());
        for (std::uint32_t instr_rva : instr_it->second) {
            refs.push_back(analyze_reference_semantics(
                image, image_size, raw_sections, sections, pdata_ranges, pdata_usable, instr_rva, slot_rva));
        }
        std::sort(refs.begin(), refs.end(), [](const GWorldReferenceSemanticDetail& a,
                                               const GWorldReferenceSemanticDetail& b) {
            return a.instruction_rva < b.instruction_rva;
        });
        const std::size_t total_recognized =
            total_reference_counts.count(slot_rva) != 0 ? total_reference_counts.at(slot_rva) : refs.size();
        GWorldSemanticCandidateRecord record =
            build_semantic_record(slot_rva, dest_section, refs, total_recognized, convergence_member, pdata_usable);
        if (convergence_member) {
            bundle.converged_records.push_back(std::move(record));
        } else {
            bundle.comparison_records.push_back(std::move(record));
        }
    };

    for (const auto& entry : context_reference_counts) {
        const bool convergence_member = entry.second >= convergence_threshold;
        if (convergence_member) {
            build_for_slot(entry.first, true);
        }
    }

    for (const std::uint32_t slot : {0x05391B70u, 0x0FE1D1D8u}) {
        if (context_reference_counts.count(slot) == 0) {
            continue;
        }
        if (context_reference_counts.at(slot) >= convergence_threshold) {
            continue;
        }
        build_for_slot(slot, false);
    }

    std::sort(bundle.converged_records.begin(),
              bundle.converged_records.end(),
              [](const GWorldSemanticCandidateRecord& a, const GWorldSemanticCandidateRecord& b) {
                  if (a.diversity.context_qualified_references != b.diversity.context_qualified_references) {
                      return a.diversity.context_qualified_references > b.diversity.context_qualified_references;
                  }
                  return a.slot_rva < b.slot_rva;
              });

    bundle.summary.converged_dataset_count = bundle.converged_records.size();
    for (const GWorldSemanticCandidateRecord& record : bundle.converged_records) {
        switch (record.classification) {
        case GWorldSemanticClassification::GWorldLike:
            ++bundle.summary.gworld_like_count;
            break;
        case GWorldSemanticClassification::Ambiguous:
            ++bundle.summary.ambiguous_count;
            break;
        case GWorldSemanticClassification::Unlikely:
            ++bundle.summary.unlikely_count;
            break;
        }
    }

    if (bundle.summary.gworld_like_count > 20) {
        bundle.summary.message =
            "Semantic evidence insufficient: " + std::to_string(bundle.summary.gworld_like_count)
            + " GWorld-like candidates exceed review threshold (20).";
    } else if (bundle.summary.gworld_like_count == 0) {
        bundle.summary.message = "No GWorld-like candidates after semantic discrimination.";
    } else {
        bundle.summary.message =
            "Semantic discrimination produced a bounded GWorld-like research set.";
    }
    return bundle;
}

} // namespace deflux::game::gta_sa_de
