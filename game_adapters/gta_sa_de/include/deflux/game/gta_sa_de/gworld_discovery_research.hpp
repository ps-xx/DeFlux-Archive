#pragma once

// Research-only GWorld discovery. Not used by production runtime qualification.
// Task #7A: offline static analysis to produce a reviewable candidate shortlist.

#include "deflux/game/gta_sa_de/qualification.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace deflux::game::gta_sa_de {

enum class GWorldReferenceForm : std::uint8_t {
    MovRegFromRip = 0,
    MovRipFromReg,
};

struct PeSectionSummary {
    char name[9]{};
    std::uint32_t virtual_address = 0;
    std::uint32_t virtual_size = 0;
    std::uint32_t characteristics = 0;
    bool executable = false;
    bool writable = false;
    bool pointer_storage_plausible = false;
};

struct GWorldReferenceEvidence {
    std::uint32_t instruction_rva = 0;
    std::uint32_t slot_rva = 0;
    GWorldReferenceForm form = GWorldReferenceForm::MovRegFromRip;
    char dest_reg[8]{};
    char source_section[9]{};
    char dest_section[9]{};
    std::uint8_t instruction_bytes[16]{};
    std::uint8_t instruction_size = 0;
    std::uint8_t context_bytes[16]{};
    bool has_null_test = false;
    bool has_null_branch = false;
};

struct GWorldSlotCandidate {
    std::uint32_t slot_rva = 0;
    std::size_t reference_count = 0;
    char dest_section[9]{};
    std::vector<GWorldReferenceEvidence> references;
    std::vector<std::string> survival_reasons;
};

struct GWorldDiscoveryStageCounts {
    std::size_t executable_bytes_scanned = 0;
    std::size_t raw_references = 0;
    std::size_t after_image_bounds = 0;
    std::size_t after_data_section = 0;
    std::size_t after_alignment = 0;
    std::size_t after_not_import = 0;
    std::size_t after_context = 0;
    std::size_t after_convergence = 0;
    std::size_t final_shortlist = 0;
};

struct GWorldLegacySlotAudit {
    std::uint32_t slot_rva = 0;
    std::size_t raw_references = 0;
    std::size_t after_image_bounds = 0;
    std::size_t after_data_section = 0;
    std::size_t after_alignment = 0;
    std::size_t after_not_import = 0;
    std::size_t after_context = 0;
    bool reached_convergence = false;
};

} // namespace deflux::game::gta_sa_de

#include "deflux/game/gta_sa_de/gworld_semantic_analysis.hpp"
#include "deflux/game/gta_sa_de/gworld_identity_analysis.hpp"

namespace deflux::game::gta_sa_de {

struct GWorldDiscoveryResearchResult {
    bool sha_verified = false;
    std::string sha256_hex;
    std::uint64_t image_base = 0;
    std::uint32_t size_of_image = 0;
    std::vector<PeSectionSummary> sections;
    GWorldDiscoveryStageCounts stages;
    std::vector<GWorldSlotCandidate> candidates;
    std::vector<GWorldLegacySlotAudit> legacy_slot_audits;
    GWorldSemanticAnalysisBundle semantic;
    GWorldIdentityAnalysisBundle identity;
    bool shortlist_acceptable = false;
    std::string message;
};

// Stricter offline discovery pipeline. Does not modify the executable.
// expected_sha256_hex: when non-empty, mismatch aborts with empty candidates.
GWorldDiscoveryResearchResult run_gworld_discovery_research(const std::uint8_t* image,
                                                            std::size_t image_size,
                                                            std::string_view expected_sha256_hex = {});

// Memory-mapped offline scan for large executables (research tooling only).
GWorldDiscoveryResearchResult run_gworld_discovery_research_file(
    std::wstring_view path,
    std::string_view expected_sha256_hex = {});

// Exposed for unit tests.
bool decode_x64_rip_relative_mov(const std::uint8_t* bytes,
                                 std::size_t available,
                                 std::uint32_t instruction_rva,
                                 GWorldReferenceEvidence& out) noexcept;

bool section_is_pointer_storage_plausible(const PeSectionSummary& section) noexcept;
bool rva_in_import_directory(const std::uint8_t* headers,
                             std::size_t header_size,
                             std::uint32_t rva) noexcept;

} // namespace deflux::game::gta_sa_de
