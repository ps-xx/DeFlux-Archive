#pragma once

// Task #7A.3: research-only UE/world identity correlation (offline, read-only).

#include "deflux/game/gta_sa_de/gworld_semantic_analysis.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace deflux::game::gta_sa_de {

enum class GWorldIdentityClassification : std::uint8_t {
    Supported = 0,
    Ambiguous,
    Contradicted,
};

enum class GWorldStringIdentityKind : std::uint8_t {
    Unknown = 0,
    WorldRelated,
    NegativeSubsystem,
};

struct GWorldStaticStringEntry {
    std::uint32_t rva = 0;
    GWorldStringIdentityKind kind = GWorldStringIdentityKind::Unknown;
    char label[64]{};
};

struct GWorldStringXrefChain {
    std::uint32_t slot_rva = 0;
    std::uint32_t slot_ref_instruction_rva = 0;
    std::uint32_t function_begin_rva = 0;
    std::uint32_t function_end_rva = 0;
    std::uint32_t string_ref_instruction_rva = 0;
    std::uint32_t string_rva = 0;
    char string_label[64]{};
    char function_identity[16]{'U', 'N', 'K', 'N', 'O', 'W', 'N', '\0'};
};

struct GWorldIdentityEvidence {
    GWorldIdentityClassification classification = GWorldIdentityClassification::Ambiguous;
    std::string classification_rationale;
    std::size_t world_string_xref_functions = 0;
    std::size_t world_related_static_xrefs = 0;
    std::size_t negative_string_xref_functions = 0;
    std::size_t negative_related_static_xrefs = 0;
    std::string identity_evidence_strength;
    bool identity_unknown = true;
    std::vector<std::string> negative_identity_evidence;
    std::vector<GWorldStringXrefChain> chains;
};

struct GWorldIdentityStringCatalogSummary {
    std::size_t world_related_strings = 0;
    std::size_t negative_strings = 0;
    std::size_t total_catalog_strings = 0;
    bool gworld_literal_present = false;
    bool uworld_literal_present = false;
    bool rtti_usable = false;
    std::string message;
};

struct GWorldIdentityAnalysisSummary {
    std::size_t gworld_like_input_count = 0;
    std::size_t identity_supported_count = 0;
    std::size_t identity_ambiguous_count = 0;
    std::size_t identity_contradicted_count = 0;
    bool real_disassembler_required = false;
    GWorldIdentityStringCatalogSummary catalog{};
    std::string message;
};

struct GWorldIdentityCandidateRecord {
    std::uint32_t slot_rva = 0;
    GWorldSemanticClassification semantic_classification = GWorldSemanticClassification::Ambiguous;
    GWorldIdentityEvidence identity{};
};

struct GWorldIdentityAnalysisBundle {
    std::vector<GWorldIdentityCandidateRecord> gworld_like_records;
    GWorldIdentityCandidateRecord legacy_comparison{};
    bool legacy_comparison_present = false;
    GWorldIdentityAnalysisSummary summary;
};

// Bounded RIP-relative LEA/MOV decode for string xref scans (unit-testable).
bool decode_x64_rip_relative_lea_or_mov(const std::uint8_t* bytes,
                                        std::size_t available,
                                        std::uint32_t instruction_rva,
                                        std::uint32_t& out_target_rva) noexcept;

GWorldIdentityClassification classify_identity_evidence(
    std::size_t world_string_xref_functions,
    std::size_t world_related_static_xrefs,
    std::size_t negative_string_xref_functions,
    std::size_t negative_related_static_xrefs,
    std::string& rationale_out);

const char* to_string(GWorldIdentityClassification classification) noexcept;

GWorldIdentityAnalysisBundle analyze_identity_evidence(
    const std::uint8_t* image,
    std::size_t image_size,
    const GWorldSemanticAnalysisBundle& semantic);

} // namespace deflux::game::gta_sa_de
