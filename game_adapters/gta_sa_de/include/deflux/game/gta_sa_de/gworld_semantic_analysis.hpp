#pragma once

// Task #7A.2: research-only semantic discrimination for GWorld-like global slots.
// Not used by production runtime qualification.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace deflux::game::gta_sa_de {

struct PeSectionSummary;

enum class GWorldSemanticClassification : std::uint8_t {
    GWorldLike = 0,
    Ambiguous,
    Unlikely,
};

enum class GWorldFunctionBoundarySource : std::uint8_t {
    Unknown = 0,
    Pdata,
    CodeRegionHeuristic,
};

struct GWorldPostLoadUsageEvidence {
    bool null_test = false;
    bool null_branch = false;
    bool post_load_dereference = false;
    bool field_access = false;
    bool indirect_vtable_like = false;
    bool register_propagation = false;
    bool scalar_like_usage = false;
    bool table_index_like = false;
    bool analysis_truncated = false;
};

struct GWorldReferenceSemanticDetail {
    std::uint32_t instruction_rva = 0;
    unsigned loaded_reg_index = 16;
    char dest_reg[8]{};
    char source_section[9]{};
    std::uint8_t instruction_bytes[7]{};
    std::uint8_t pre_context_bytes[8]{};
    std::uint8_t post_context_bytes[32]{};
    GWorldPostLoadUsageEvidence usage{};
    GWorldFunctionBoundarySource boundary_source = GWorldFunctionBoundarySource::Unknown;
    std::uint32_t function_begin_rva = 0;
    std::uint32_t function_end_rva = 0;
    std::uint32_t code_region_id = 0;
};

struct GWorldStructuralEvidence {
    bool in_pointer_storage_section = false;
    bool aligned_8 = false;
    bool in_image = false;
    bool non_iat = false;
};

struct GWorldReferenceDiversityEvidence {
    std::size_t total_recognized_references = 0;
    std::size_t context_qualified_references = 0;
    std::size_t unique_instruction_rvas = 0;
    std::size_t unique_code_regions = 0;
    std::size_t unique_pdata_functions = 0;
    bool references_concentrated_in_one_region = false;
    bool pdata_boundaries_available = false;
};

struct GWorldObjectUsageEvidence {
    std::size_t refs_with_post_load_dereference = 0;
    std::size_t refs_with_field_access = 0;
    std::size_t refs_with_vtable_like = 0;
    std::size_t refs_with_register_propagation = 0;
    std::size_t refs_with_any_object_usage = 0;
};

struct GWorldControlFlowEvidence {
    std::size_t refs_with_null_test = 0;
    std::size_t refs_with_null_branch = 0;
    bool repeated_load_test_branch_pattern = false;
};

struct GWorldNegativeEvidence {
    std::size_t refs_with_scalar_like_usage = 0;
    std::size_t refs_with_table_index_like = 0;
    std::size_t refs_without_object_usage = 0;
    bool scalar_dominant = false;
};

struct GWorldSemanticCandidateRecord {
    std::uint32_t slot_rva = 0;
    char dest_section[9]{};
    GWorldSemanticClassification classification = GWorldSemanticClassification::Ambiguous;
    std::string classification_rationale;
    GWorldStructuralEvidence structural{};
    GWorldReferenceDiversityEvidence diversity{};
    GWorldObjectUsageEvidence object_usage{};
    GWorldControlFlowEvidence control_flow{};
    GWorldNegativeEvidence negative{};
    std::vector<std::string> dest_registers;
    std::vector<GWorldReferenceSemanticDetail> reference_details;
    bool convergence_dataset_member = false;
    std::string convergence_note;
};

struct GWorldPdataCoverage {
    bool parsed = false;
    bool usable = false;
    std::size_t runtime_function_count = 0;
    std::string message;
};

struct GWorldSemanticAnalysisSummary {
    std::size_t converged_dataset_count = 0;
    std::size_t gworld_like_count = 0;
    std::size_t ambiguous_count = 0;
    std::size_t unlikely_count = 0;
    GWorldPdataCoverage pdata{};
    std::string message;
};

// Bounded post-load usage scan (unit-testable).
GWorldPostLoadUsageEvidence analyze_bounded_post_load_usage(const std::uint8_t* bytes,
                                                            std::size_t available,
                                                            unsigned loaded_reg_index) noexcept;

unsigned decode_mov_rip_dest_reg_index(const std::uint8_t* bytes, std::size_t available) noexcept;

GWorldSemanticClassification classify_semantic_candidate(
    const GWorldStructuralEvidence& structural,
    const GWorldReferenceDiversityEvidence& diversity,
    const GWorldObjectUsageEvidence& object_usage,
    const GWorldControlFlowEvidence& control_flow,
    const GWorldNegativeEvidence& negative,
    std::string& rationale_out);

const char* to_string(GWorldSemanticClassification classification) noexcept;
const char* to_string(GWorldFunctionBoundarySource source) noexcept;

struct GWorldSemanticAnalysisBundle {
    std::vector<GWorldSemanticCandidateRecord> converged_records;
    std::vector<GWorldSemanticCandidateRecord> comparison_records;
    GWorldSemanticAnalysisSummary summary;
};

GWorldSemanticAnalysisBundle analyze_semantic_candidates(
    const std::uint8_t* image,
    std::size_t image_size,
    const std::vector<PeSectionSummary>& sections,
    const std::unordered_map<std::uint32_t, std::size_t>& context_reference_counts,
    const std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>& context_instruction_rvas,
    const std::unordered_map<std::uint32_t, std::array<char, 9>>& slot_dest_sections,
    const std::unordered_map<std::uint32_t, std::size_t>& total_reference_counts,
    std::size_t convergence_threshold);

} // namespace deflux::game::gta_sa_de
