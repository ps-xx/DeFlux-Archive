#pragma once

// Task #8E: offline read-only local-player semantic correlation research.
// Not used by production runtime qualification, readiness, or supported registry.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace deflux::game::gta_sa_de {

inline constexpr const char kLocalPlayerSemanticTargetSha256[] =
    "63e633bb51c35fe35ca98f503b62e7ddb8403abe4c9d501cfa9162892453846f";

inline constexpr std::size_t kSemanticMaxDirectXrefs = 64;
inline constexpr std::size_t kSemanticMaxPdataFunctions = 64;
inline constexpr std::size_t kSemanticMaxFunctionWindow = 0x1000;
inline constexpr std::size_t kSemanticMaxGlobalRefsPerAnchor = 128;

enum class SemanticCandidateKind : std::uint8_t {
    Function = 0,
    Global,
    FieldOffset,
    Relationship,
};

enum class SemanticEvidenceClass : std::uint8_t {
    Strong = 0,
    PromisingUnverified,
    Weak,
    Contradicted,
};

enum class SemanticFieldAccessType : std::uint8_t {
    Read = 0,
    Write,
    AddressCalculation,
};

struct SemanticAnchorSpec {
    const char* text;
    std::uint8_t priority;
    bool secondary;
};

struct SemanticStringLocation {
    const char* anchor = "";
    std::uint32_t string_rva = 0;
    char section[9]{};
};

struct SemanticDirectXref {
    const char* anchor = "";
    std::uint32_t instruction_rva = 0;
    std::uint32_t string_rva = 0;
    std::uint8_t instruction_bytes[7]{};
};

struct SemanticFunctionRecord {
    std::uint32_t begin_rva = 0;
    std::uint32_t end_rva = 0;
    std::uint32_t window_bytes = 0;
    std::vector<std::string> anchors_present;
    std::vector<std::uint32_t> xref_instruction_rvas;
    SemanticEvidenceClass classification = SemanticEvidenceClass::Weak;
    std::string classification_rationale;
    std::string provenance;
};

struct SemanticGlobalRecord {
    std::uint32_t global_rva = 0;
    char section[9]{};
    SemanticFieldAccessType access = SemanticFieldAccessType::Read;
    std::vector<std::uint32_t> referencing_function_rvas;
    std::vector<std::string> associated_anchors;
    SemanticEvidenceClass classification = SemanticEvidenceClass::Weak;
    std::string provenance;
};

struct SemanticFieldOffsetRecord {
    std::uint32_t function_begin_rva = 0;
    std::uint32_t instruction_rva = 0;
    SemanticFieldAccessType access = SemanticFieldAccessType::Read;
    std::uint32_t candidate_offset = 0;
    char base_register[8]{};
    std::string anchor_relationship;
    std::string evidence;
    std::string competing_interpretations;
    SemanticEvidenceClass classification = SemanticEvidenceClass::Weak;
};

struct SemanticRelationshipRecord {
    std::uint32_t function_begin_rva = 0;
    std::vector<std::string> anchors;
    std::vector<std::uint32_t> shared_global_rvas;
    std::string description;
    SemanticEvidenceClass classification = SemanticEvidenceClass::Weak;
    std::string provenance;
};

struct SemanticAnchorAnalysis {
    const char* anchor = "";
    std::size_t string_occurrences = 0;
    std::size_t direct_xrefs = 0;
    std::size_t pdata_functions = 0;
    bool xrefs_truncated = false;
    bool functions_truncated = false;
    bool global_refs_truncated = false;
    std::vector<SemanticStringLocation> strings;
    std::vector<SemanticDirectXref> xrefs;
    std::vector<SemanticFunctionRecord> functions;
    std::vector<SemanticGlobalRecord> globals;
    std::vector<SemanticFieldOffsetRecord> field_offsets;
};

struct SemanticAnalysisBoundsReport {
    bool any_truncation = false;
    std::vector<std::string> truncation_notes;
};

struct SemanticAnalysisSummary {
    bool sha_verified = false;
    std::uint64_t image_size = 0;
    std::string message;
    SemanticAnalysisBoundsReport bounds;
    std::vector<SemanticAnchorAnalysis> anchors;
    std::vector<SemanticRelationshipRecord> relationships;
    bool justified_live_candidate = false;
    std::string live_candidate_rationale;
    std::string recommended_live_experiment;
};

const char* to_string(SemanticCandidateKind kind) noexcept;
const char* to_string(SemanticEvidenceClass classification) noexcept;
const char* to_string(SemanticFieldAccessType access) noexcept;

std::vector<SemanticAnchorSpec> semantic_anchor_catalog() noexcept;

SemanticAnalysisSummary analyze_local_player_semantic_image(const std::uint8_t* image,
                                                            std::size_t image_size,
                                                            std::string_view expected_sha256_hex);

SemanticAnalysisSummary run_local_player_semantic_research_file(std::wstring_view path,
                                                                std::string_view expected_sha256_hex);

std::vector<std::string> format_local_player_semantic_report(
    const SemanticAnalysisSummary& summary);

} // namespace deflux::game::gta_sa_de
