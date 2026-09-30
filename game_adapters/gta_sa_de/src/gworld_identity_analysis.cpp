#include "deflux/game/gta_sa_de/gworld_identity_analysis.hpp"

#include "deflux/game/gta_sa_de/gworld_discovery_research.hpp"
#include "gworld_discovery.hpp"
#include "pe_image.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace deflux::game::gta_sa_de {
namespace {

constexpr std::size_t kMaxFunctionScanSpan = 128 * 1024;
constexpr std::size_t kMinStringLen = 4;

struct RuntimeFunctionRange {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

struct StringPattern {
    const char* text;
    GWorldStringIdentityKind kind;
};

struct FunctionStringXref {
    std::uint32_t instruction_rva = 0;
    std::uint32_t string_rva = 0;
    char label[64]{};
    GWorldStringIdentityKind kind = GWorldStringIdentityKind::Unknown;
};

void copy_label(char* dest, std::size_t dest_size, const char* src) noexcept {
    if (dest == nullptr || dest_size == 0) {
        return;
    }
    std::memset(dest, 0, dest_size);
    if (src == nullptr) {
        return;
    }
    const std::size_t copy_len = std::min(dest_size - 1, std::strlen(src));
    std::memcpy(dest, src, copy_len);
}

constexpr StringPattern kPatterns[] = {
    {"PersistentLevel", GWorldStringIdentityKind::WorldRelated},
    {"OwningGameInstance", GWorldStringIdentityKind::WorldRelated},
    {"GameInstance", GWorldStringIdentityKind::WorldRelated},
    {"WorldContext", GWorldStringIdentityKind::WorldRelated},
    {"NetDriver", GWorldStringIdentityKind::WorldRelated},
    {"GameViewport", GWorldStringIdentityKind::WorldRelated},
    {"GEngine", GWorldStringIdentityKind::WorldRelated},
    {"GUObjectArray", GWorldStringIdentityKind::WorldRelated},
    {"GWorld", GWorldStringIdentityKind::WorldRelated},
    {"UWorld", GWorldStringIdentityKind::WorldRelated},
    {"LogTemp", GWorldStringIdentityKind::NegativeSubsystem},
    {"FMemory", GWorldStringIdentityKind::NegativeSubsystem},
    {"Localization", GWorldStringIdentityKind::NegativeSubsystem},
    {"SlateApplication", GWorldStringIdentityKind::NegativeSubsystem},
    {"RenderThread", GWorldStringIdentityKind::NegativeSubsystem},
    {"ConsoleManager", GWorldStringIdentityKind::NegativeSubsystem},
    {"ConfigCache", GWorldStringIdentityKind::NegativeSubsystem},
    {"ThreadPool", GWorldStringIdentityKind::NegativeSubsystem},
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

bool section_is_readonly_data(const PeSectionView& section) noexcept {
    if (section.executable) {
        return false;
    }
    return std::strncmp(section.name, ".rdata", 6) == 0
        || std::strncmp(section.name, ".data", 5) == 0
        || std::strncmp(section.name, "_RDATA", 6) == 0;
}

const GWorldStaticStringEntry* find_string_entry(const std::vector<GWorldStaticStringEntry>& catalog,
                                                 std::uint32_t rva) noexcept {
    for (const GWorldStaticStringEntry& entry : catalog) {
        const std::size_t len = std::strlen(entry.label);
        if (len == 0) {
            continue;
        }
        if (rva >= entry.rva && rva < entry.rva + len) {
            return &entry;
        }
    }
    return nullptr;
}

void build_string_catalog(const std::uint8_t* image,
                          std::size_t image_size,
                          const std::vector<PeSectionView>& sections,
                          std::vector<GWorldStaticStringEntry>& catalog,
                          GWorldIdentityStringCatalogSummary& summary) {
    catalog.clear();
    summary = {};

    for (const PeSectionView& section : sections) {
        if (!section_is_readonly_data(section)) {
            continue;
        }
        if (section.raw_pointer > image_size || section.raw_size > image_size - section.raw_pointer) {
            continue;
        }
        const std::uint8_t* blob = image + section.raw_pointer;
        const std::size_t blob_size = section.raw_size;
        for (const StringPattern& pattern : kPatterns) {
            const std::size_t pat_len = std::strlen(pattern.text);
            if (pat_len < kMinStringLen) {
                continue;
            }
            for (std::size_t i = 0; i + pat_len <= blob_size; ++i) {
                if (std::memcmp(blob + i, pattern.text, pat_len) != 0) {
                    continue;
                }
                GWorldStaticStringEntry entry{};
                entry.rva = section.virtual_address + static_cast<std::uint32_t>(i);
                entry.kind = pattern.kind;
                copy_label(entry.label, sizeof(entry.label), pattern.text);
                catalog.push_back(entry);
                if (pattern.kind == GWorldStringIdentityKind::WorldRelated) {
                    ++summary.world_related_strings;
                } else if (pattern.kind == GWorldStringIdentityKind::NegativeSubsystem) {
                    ++summary.negative_strings;
                }
                if (std::strcmp(pattern.text, "GWorld") == 0) {
                    summary.gworld_literal_present = true;
                }
                if (std::strcmp(pattern.text, "UWorld") == 0) {
                    summary.uworld_literal_present = true;
                }
            }
        }

        for (std::size_t i = 0; i + 8 < blob_size; ++i) {
            if (blob[i] == '.' && blob[i + 1] == '?' && blob[i + 2] == 'A' && blob[i + 3] == 'V') {
                if (i + 16 <= blob_size && std::memcmp(blob + i + 4, "UWorld@", 7) == 0) {
                    summary.rtti_usable = true;
                }
            }
        }
    }

    std::sort(catalog.begin(), catalog.end(), [](const GWorldStaticStringEntry& a, const GWorldStaticStringEntry& b) {
        return a.rva < b.rva;
    });
    catalog.erase(std::unique(catalog.begin(),
                              catalog.end(),
                              [](const GWorldStaticStringEntry& a, const GWorldStaticStringEntry& b) {
                                  return a.rva == b.rva && std::strcmp(a.label, b.label) == 0;
                              }),
                  catalog.end());
    summary.total_catalog_strings = catalog.size();
    summary.message = summary.total_catalog_strings > 0
        ? "Deterministic string catalog built from read-only PE sections."
        : "No catalogued UE/world strings found in read-only sections.";
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
    if (pos >= available) {
        return 0;
    }
    const std::uint8_t opcode = bytes[pos++];
    if (opcode != 0x8B && opcode != 0x8D) {
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

std::vector<FunctionStringXref> scan_function_string_xrefs(
    const std::uint8_t* image,
    std::size_t image_size,
    const std::vector<PeSectionView>& sections,
    const std::vector<GWorldStaticStringEntry>& catalog,
    std::uint32_t function_begin,
    std::uint32_t function_end) {
    std::vector<FunctionStringXref> out;
    if (function_end <= function_begin || function_end - function_begin > kMaxFunctionScanSpan) {
        return out;
    }
    const std::size_t span = static_cast<std::size_t>(function_end - function_begin);
    std::vector<std::uint8_t> bytes(span);
    std::size_t written = 0;
    if (!read_bytes_at_rva(image, image_size, sections, function_begin, bytes.data(), bytes.size(), written)
        || written < 7) {
        return out;
    }

    std::unordered_set<std::uint64_t> seen;
    for (std::size_t i = 0; i + 7 <= written;) {
        const std::uint8_t b0 = bytes[i];
        if (b0 != 0x48 && b0 != 0x4C) {
            ++i;
            continue;
        }
        if (i + 2 >= written) {
            break;
        }
        const std::uint8_t opcode = bytes[i + 1];
        if (opcode != 0x8D && opcode != 0x8B) {
            ++i;
            continue;
        }
        std::uint32_t target = 0;
        if (!decode_x64_rip_relative_lea_or_mov(bytes.data() + i,
                                                written - i,
                                                function_begin + static_cast<std::uint32_t>(i),
                                                target)) {
            ++i;
            continue;
        }
        const GWorldStaticStringEntry* entry = find_string_entry(catalog, target);
        if (entry != nullptr) {
            const std::uint64_t key =
                (static_cast<std::uint64_t>(function_begin + static_cast<std::uint32_t>(i)) << 32)
                | static_cast<std::uint64_t>(entry->rva);
            if (seen.insert(key).second) {
                FunctionStringXref xref{};
                xref.instruction_rva = function_begin + static_cast<std::uint32_t>(i);
                xref.string_rva = entry->rva;
                xref.kind = entry->kind;
                copy_label(xref.label, sizeof(xref.label), entry->label);
                out.push_back(xref);
            }
        }
        const std::uint32_t adv = bounded_instr_len(bytes.data(), written, i);
        i += adv > 0 ? adv : 1;
    }
    return out;
}

GWorldIdentityEvidence build_identity_for_candidate(
    const GWorldSemanticCandidateRecord& semantic_record,
    const std::unordered_map<std::uint32_t, std::vector<FunctionStringXref>>& function_xrefs) {
    GWorldIdentityEvidence evidence{};
    std::unordered_set<std::uint32_t> world_functions;
    std::unordered_set<std::uint32_t> negative_functions;

    for (const GWorldReferenceSemanticDetail& ref : semantic_record.reference_details) {
        if (ref.boundary_source != GWorldFunctionBoundarySource::Pdata) {
            continue;
        }
        const auto it = function_xrefs.find(ref.function_begin_rva);
        if (it == function_xrefs.end()) {
            continue;
        }
        for (const FunctionStringXref& fx : it->second) {
            GWorldStringXrefChain chain{};
            chain.slot_rva = semantic_record.slot_rva;
            chain.slot_ref_instruction_rva = ref.instruction_rva;
            chain.function_begin_rva = ref.function_begin_rva;
            chain.function_end_rva = ref.function_end_rva;
            chain.string_ref_instruction_rva = fx.instruction_rva;
            chain.string_rva = fx.string_rva;
            copy_label(chain.string_label, sizeof(chain.string_label), fx.label);
            evidence.chains.push_back(chain);

            if (fx.kind == GWorldStringIdentityKind::WorldRelated) {
                world_functions.insert(ref.function_begin_rva);
                ++evidence.world_related_static_xrefs;
            } else if (fx.kind == GWorldStringIdentityKind::NegativeSubsystem) {
                negative_functions.insert(ref.function_begin_rva);
                ++evidence.negative_related_static_xrefs;
                char fn_buf[16]{};
                std::snprintf(fn_buf, sizeof(fn_buf), "%X", ref.function_begin_rva);
                evidence.negative_identity_evidence.push_back(
                    std::string("function 0x") + fn_buf + " references " + fx.label);
            }
        }
    }

    evidence.world_string_xref_functions = world_functions.size();
    evidence.negative_string_xref_functions = negative_functions.size();
    evidence.identity_unknown =
        evidence.world_related_static_xrefs == 0 && evidence.negative_related_static_xrefs == 0;

    if (evidence.world_related_static_xrefs >= 1) {
        evidence.identity_evidence_strength =
            evidence.world_related_static_xrefs >= 3 ? "moderate" : "weak";
    } else {
        evidence.identity_evidence_strength = "none";
    }

    evidence.classification = classify_identity_evidence(
        evidence.world_string_xref_functions,
        evidence.world_related_static_xrefs,
        evidence.negative_string_xref_functions,
        evidence.negative_related_static_xrefs,
        evidence.classification_rationale);

    std::sort(evidence.negative_identity_evidence.begin(), evidence.negative_identity_evidence.end());
    evidence.negative_identity_evidence.erase(
        std::unique(evidence.negative_identity_evidence.begin(), evidence.negative_identity_evidence.end()),
        evidence.negative_identity_evidence.end());

    return evidence;
}

void collect_unique_pdata_functions(
    const GWorldSemanticCandidateRecord& record,
    std::unordered_map<std::uint32_t, std::uint32_t>& function_ends) {
    for (const GWorldReferenceSemanticDetail& ref : record.reference_details) {
        if (ref.boundary_source != GWorldFunctionBoundarySource::Pdata || ref.function_begin_rva == 0) {
            continue;
        }
        function_ends[ref.function_begin_rva] = ref.function_end_rva;
    }
}

} // namespace

bool decode_x64_rip_relative_lea_or_mov(const std::uint8_t* bytes,
                                        std::size_t available,
                                        std::uint32_t instruction_rva,
                                        std::uint32_t& out_target_rva) noexcept {
    out_target_rva = 0;
    if (bytes == nullptr || available < 7) {
        return false;
    }
    std::size_t pos = 0;
    if (bytes[0] >= 0x40 && bytes[0] <= 0x4F) {
        ++pos;
        if (pos + 6 > available) {
            return false;
        }
    }
    const std::uint8_t opcode = bytes[pos];
    if (opcode != 0x8D && opcode != 0x8B) {
        return false;
    }
    const std::uint8_t modrm = bytes[pos + 1];
    if ((modrm & 0xC7u) != 0x05u) {
        return false;
    }
    const std::uint32_t len = bounded_instr_len(bytes, available, 0);
    if (len < 7) {
        return false;
    }
    const std::uint32_t target = decode_x64_rip_relative(
        instruction_rva, bytes, static_cast<std::uint32_t>(pos + 2), len);
    if (target == 0) {
        return false;
    }
    out_target_rva = target;
    return true;
}

GWorldIdentityClassification classify_identity_evidence(
    std::size_t world_string_xref_functions,
    std::size_t world_related_static_xrefs,
    std::size_t negative_string_xref_functions,
    std::size_t negative_related_static_xrefs,
    std::string& rationale_out) {
    rationale_out.clear();
    if (world_related_static_xrefs >= 1 && world_string_xref_functions >= 1) {
        rationale_out =
            "At least one .pdata function referencing the slot also contains a bounded RIP-relative "
            "reference to a catalogued world-related string.";
        return GWorldIdentityClassification::Supported;
    }
    if (negative_related_static_xrefs >= 2 && negative_string_xref_functions >= 1
        && world_related_static_xrefs == 0) {
        rationale_out =
            "Referencing functions contain catalogued negative subsystem string xrefs without any "
            "world-related string correlation.";
        return GWorldIdentityClassification::Contradicted;
    }
    rationale_out =
        world_related_static_xrefs == 0
        ? "No defensible code-reference chain from slot-referencing .pdata functions to world-related strings."
        : "Insufficient identity evidence for a supported or contradicted classification.";
    return GWorldIdentityClassification::Ambiguous;
}

const char* to_string(GWorldIdentityClassification classification) noexcept {
    switch (classification) {
    case GWorldIdentityClassification::Supported:
        return "IDENTITY_SUPPORTED";
    case GWorldIdentityClassification::Ambiguous:
        return "IDENTITY_AMBIGUOUS";
    case GWorldIdentityClassification::Contradicted:
        return "IDENTITY_CONTRADICTED";
    }
    return "UNKNOWN";
}

GWorldIdentityAnalysisBundle analyze_identity_evidence(
    const std::uint8_t* image,
    std::size_t image_size,
    const GWorldSemanticAnalysisBundle& semantic) {
    GWorldIdentityAnalysisBundle bundle{};

    if (image == nullptr || image_size == 0) {
        bundle.summary.message = "Image buffer is empty.";
        return bundle;
    }

    std::vector<PeSectionView> sections;
    if (!pe_enumerate_sections(image, image_size, sections)) {
        bundle.summary.message = "Failed to enumerate PE sections for identity analysis.";
        return bundle;
    }

    std::vector<GWorldStaticStringEntry> catalog;
    build_string_catalog(image, image_size, sections, catalog, bundle.summary.catalog);

    std::vector<RuntimeFunctionRange> pdata_ranges;
    const bool pdata_usable = parse_pdata_ranges(image, image_size, sections, pdata_ranges);

    std::unordered_map<std::uint32_t, std::uint32_t> unique_functions;
    for (const GWorldSemanticCandidateRecord& record : semantic.converged_records) {
        if (record.classification == GWorldSemanticClassification::GWorldLike) {
            collect_unique_pdata_functions(record, unique_functions);
        }
    }
    for (const GWorldSemanticCandidateRecord& record : semantic.comparison_records) {
        collect_unique_pdata_functions(record, unique_functions);
    }

    std::unordered_map<std::uint32_t, std::vector<FunctionStringXref>> function_xrefs;
    if (pdata_usable) {
        for (const auto& entry : unique_functions) {
            function_xrefs[entry.first] =
                scan_function_string_xrefs(image, image_size, sections, catalog, entry.first, entry.second);
        }
    }

    bundle.summary.real_disassembler_required = false;

    for (const GWorldSemanticCandidateRecord& record : semantic.converged_records) {
        if (record.classification != GWorldSemanticClassification::GWorldLike) {
            continue;
        }
        ++bundle.summary.gworld_like_input_count;
        GWorldIdentityCandidateRecord out{};
        out.slot_rva = record.slot_rva;
        out.semantic_classification = record.classification;
        out.identity = build_identity_for_candidate(record, function_xrefs);
        switch (out.identity.classification) {
        case GWorldIdentityClassification::Supported:
            ++bundle.summary.identity_supported_count;
            break;
        case GWorldIdentityClassification::Ambiguous:
            ++bundle.summary.identity_ambiguous_count;
            break;
        case GWorldIdentityClassification::Contradicted:
            ++bundle.summary.identity_contradicted_count;
            break;
        }
        bundle.gworld_like_records.push_back(std::move(out));
    }

    for (const GWorldSemanticCandidateRecord& record : semantic.comparison_records) {
        if (record.slot_rva != 0x05391B70u) {
            continue;
        }
        bundle.legacy_comparison_present = true;
        bundle.legacy_comparison.slot_rva = record.slot_rva;
        bundle.legacy_comparison.semantic_classification = record.classification;
        bundle.legacy_comparison.identity = build_identity_for_candidate(record, function_xrefs);
        break;
    }

    if (bundle.summary.identity_supported_count > 20) {
        bundle.summary.message =
            "Identity evidence insufficient: " + std::to_string(bundle.summary.identity_supported_count)
            + " IDENTITY_SUPPORTED candidates exceed review threshold (20).";
    } else if (bundle.summary.identity_supported_count == 0) {
        bundle.summary.message =
            "No IDENTITY_SUPPORTED candidates. Offline static identity correlation did not isolate UWorld/GWorld.";
    } else {
        bundle.summary.message =
            "Identity correlation produced a bounded IDENTITY_SUPPORTED research set.";
    }
    return bundle;
}

} // namespace deflux::game::gta_sa_de
