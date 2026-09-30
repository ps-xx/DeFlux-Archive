#pragma once

// Task #9B Gate 2B: append-only research evidence log (outside GTA/Rockstar paths).

#include "deflux/game/gta_sa_de/in_process_research.hpp"

#include <cstdint>
#include <string>

namespace deflux::game::gta_sa_de {

struct ResearchEvidenceSessionHeader {
    std::uint32_t process_id = 0;
    std::uint64_t creation_identity = 0;
    std::uint64_t session_nonce = 0;
    const char* experiment_id = "";
    std::uint32_t experiment_version = 0;
    std::string expected_sha256;
    std::string observed_sha256;
    bool pipeline_proof = false;
};

void research_evidence_set_hook_enabled(bool enabled) noexcept;

bool research_evidence_begin_session(const ResearchEvidenceSessionHeader& header,
                                     std::string& log_path_out) noexcept;

void research_evidence_append_sample(const InProcessResearchEvidenceSample& sample) noexcept;

void research_evidence_end_session(ResearchSubstate final_substate,
                                   InProcessSignalClassification classification,
                                   const std::string& reason) noexcept;

// Coarse lifecycle breadcrumbs (research-only). Always fflush. Dual-writes to:
// 1) session evidence log when a session is open
// 2) lifecycle_<pid>.log in the evidence directory (survives abrupt death)
void research_evidence_append_lifecycle(const char* event,
                                        const char* detail = "") noexcept;

std::string research_evidence_default_directory_utf8() noexcept;

void research_evidence_set_output_directory_override(const std::string& path) noexcept;

void research_evidence_clear_output_directory_override() noexcept;

} // namespace deflux::game::gta_sa_de
