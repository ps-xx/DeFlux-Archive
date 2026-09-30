#include "deflux/core/session_state.hpp"
#include "deflux/game/adapter.hpp"
#include "deflux/game/gta_sa_de/adapter.hpp"
#include "deflux/game/gta_sa_de/gworld_discovery_research.hpp"
#include "deflux/game/gta_sa_de/ggame_state_observation.hpp"
#include "deflux/game/gta_sa_de/gworld_live_observation.hpp"
#include "deflux/game/gta_sa_de/legacy_chain_diagnostic.hpp"
#include "deflux/game/gta_sa_de/local_player_semantic_research.hpp"
#include "deflux/version/version.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

void print_version(const char* label, const deflux::version::SemanticVersion& version) {
    char text[32]{};
    if (!deflux::version::format(version, text, sizeof(text))) {
        std::printf("%s: <format-failed>\n", label);
        return;
    }
    std::printf("%s: %s\n", label, text);
}

void print_readiness(const char* label, const deflux::game::ReadinessResult& result) {
    std::printf("  %s: %s (%s)\n",
                label,
                deflux::game::to_string(result.status),
                result.message != nullptr ? result.message : "");
}

void print_qualification(const deflux::game::gta_sa_de::QualificationResult& qualification) {
    std::printf("\nRuntime Qualification:\n");
    std::printf("  status: %s\n", deflux::game::gta_sa_de::to_string(qualification.status));
    if (qualification.status == deflux::game::gta_sa_de::QualificationStatus::NotRunning) {
        std::printf("  build: n/a\n");
        std::printf("  module: n/a\n");
        std::printf("  anchor: n/a\n");
    } else if (qualification.status
               == deflux::game::gta_sa_de::QualificationStatus::EnvironmentContaminated) {
        std::printf("  build: n/a\n");
        std::printf("  module: n/a\n");
        std::printf("  anchor: n/a\n");
        std::printf("  preflight: failed (legacy artifacts in executable directory)\n");
    } else {
        const char* build = "mismatch";
        if (qualification.status == deflux::game::gta_sa_de::QualificationStatus::ProfileMissing) {
            build = "no-profile";
        } else if (qualification.build_exact_match) {
            build = "exact-match";
        }
        std::printf("  build: %s\n", build);
        std::printf("  module: %s\n", qualification.module_valid ? "valid" : "invalid");
        std::printf("  anchor: %s\n", qualification.anchor_valid ? "qualified" : "not qualified");
    }
    std::printf("  message: %s\n", qualification.message.c_str());
}

void print_investigation(const deflux::game::gta_sa_de::QualificationInvestigation& report) {
    print_qualification(report.qualification);
    std::printf("\nRuntime Qualification Investigation (developer):\n");
    std::printf("  summary: %s\n", report.summary.c_str());
    std::printf("  size_of_image: 0x%X\n", report.size_of_image);
    std::printf("  profile_section: %s\n", report.profile_section[0] != '\0' ? report.profile_section : "(n/a)");
    std::printf("  profile_slot_read_ok: %s\n", report.profile_slot_read_ok ? "yes" : "no");
    if (report.profile_slot_read_ok) {
        std::printf("  profile_slot_is_null: %s\n", report.profile_slot_value == 0 ? "yes" : "no");
    }
    std::printf("  offline_signature_matches: %zu\n", report.offline_discovery.matches.size());
    std::printf("  offline_unique_slot_rvas: %zu\n", report.offline_discovery.unique_slot_count);
    std::printf("  live_signature_matches: %zu\n", report.live_discovery.matches.size());
    std::printf("  live_unique_slot_rvas: %zu\n", report.live_discovery.unique_slot_count);
    std::printf("  signature_unique_slot_rva: 0x%X\n", report.signature_unique_slot_rva);
    std::printf("  profile_rva_signature_hits: %zu\n", report.profile_rva_signature_hits);
    std::printf("  profile_rva_matches_signature: %s\n",
                report.profile_rva_matches_signature ? "yes" : "no");
    if (report.signature_unique_slot_count != 1) {
        std::printf("  note: signature discovery did not resolve exactly one slot RVA.\n");
    }
}

void print_gworld_research_report(const deflux::game::gta_sa_de::GWorldDiscoveryResearchResult& result) {
    std::printf("GWorld Discovery Research (offline)\n");
    std::printf("  sha256: %s\n", result.sha256_hex.c_str());
    std::printf("  sha_verified: %s\n", result.sha_verified ? "yes" : "no");
    std::printf("  image_base: 0x%llX\n", static_cast<unsigned long long>(result.image_base));
    std::printf("  size_of_image: 0x%X\n", result.size_of_image);
    std::printf("  message: %s\n", result.message.c_str());
    std::printf("  shortlist_acceptable: %s\n", result.shortlist_acceptable ? "yes" : "no");
    std::printf("\nPE sections:\n");
    for (const auto& section : result.sections) {
        std::printf("  %s rva=0x%X size=0x%X exec=%d write=%d ptr_storage=%d\n",
                    section.name,
                    section.virtual_address,
                    section.virtual_size,
                    section.executable ? 1 : 0,
                    section.writable ? 1 : 0,
                    section.pointer_storage_plausible ? 1 : 0);
    }
    std::printf("\nFilter stages:\n");
    std::printf("  executable_bytes_scanned=%zu\n", result.stages.executable_bytes_scanned);
    std::printf("  raw=%zu bounds=%zu data=%zu align=%zu import=%zu context=%zu converge=%zu final=%zu\n",
                result.stages.raw_references,
                result.stages.after_image_bounds,
                result.stages.after_data_section,
                result.stages.after_alignment,
                result.stages.after_not_import,
                result.stages.after_context,
                result.stages.after_convergence,
                result.stages.final_shortlist);
    std::printf("\nLegacy slot audits (no algorithmic preference):\n");
    for (const auto& audit : result.legacy_slot_audits) {
        std::printf("  slot=0x%08X raw=%zu bounds=%zu data=%zu align=%zu import=%zu context=%zu converge=%s\n",
                    audit.slot_rva,
                    audit.raw_references,
                    audit.after_image_bounds,
                    audit.after_data_section,
                    audit.after_alignment,
                    audit.after_not_import,
                    audit.after_context,
                    audit.reached_convergence ? "yes" : "no");
    }
    std::printf("\nSemantic discrimination (Task #7A.2):\n");
    std::printf("  converged_dataset=%zu gworld_like=%zu ambiguous=%zu unlikely=%zu\n",
                result.semantic.summary.converged_dataset_count,
                result.semantic.summary.gworld_like_count,
                result.semantic.summary.ambiguous_count,
                result.semantic.summary.unlikely_count);
    std::printf("  pdata_parsed=%s pdata_usable=%s runtime_functions=%zu\n",
                result.semantic.summary.pdata.parsed ? "yes" : "no",
                result.semantic.summary.pdata.usable ? "yes" : "no",
                result.semantic.summary.pdata.runtime_function_count);
    std::printf("  semantic_message: %s\n", result.semantic.summary.message.c_str());
    std::printf("\nGWorld-like research candidates:\n");
    for (const auto& record : result.semantic.converged_records) {
        if (record.classification != deflux::game::gta_sa_de::GWorldSemanticClassification::GWorldLike) {
            continue;
        }
        std::printf("  slot=0x%08X section=%s ctx_refs=%zu unique_instr=%zu unique_regions=%zu unique_pdata_fn=%zu\n",
                    record.slot_rva,
                    record.dest_section,
                    record.diversity.context_qualified_references,
                    record.diversity.unique_instruction_rvas,
                    record.diversity.unique_code_regions,
                    record.diversity.unique_pdata_functions);
        std::printf("    object_refs=%zu scalar_refs=%zu rationale=%s\n",
                    record.object_usage.refs_with_any_object_usage,
                    record.negative.refs_with_scalar_like_usage,
                    record.classification_rationale.c_str());
    }
    std::printf("\nIdentity correlation (Task #7A.3):\n");
    std::printf("  catalog_world_strings=%zu catalog_negative_strings=%zu gworld_literal=%s uworld_literal=%s rtti_usable=%s\n",
                result.identity.summary.catalog.world_related_strings,
                result.identity.summary.catalog.negative_strings,
                result.identity.summary.catalog.gworld_literal_present ? "yes" : "no",
                result.identity.summary.catalog.uworld_literal_present ? "yes" : "no",
                result.identity.summary.catalog.rtti_usable ? "yes" : "no");
    std::printf("  real_disassembler_required=%s\n",
                result.identity.summary.real_disassembler_required ? "yes" : "no");
    std::printf("  identity_supported=%zu identity_ambiguous=%zu identity_contradicted=%zu\n",
                result.identity.summary.identity_supported_count,
                result.identity.summary.identity_ambiguous_count,
                result.identity.summary.identity_contradicted_count);
    std::printf("  identity_message: %s\n", result.identity.summary.message.c_str());
    std::printf("\nIDENTITY_SUPPORTED candidates:\n");
    for (const auto& record : result.identity.gworld_like_records) {
        if (record.identity.classification
            != deflux::game::gta_sa_de::GWorldIdentityClassification::Supported) {
            continue;
        }
        std::printf("  slot=0x%08X strength=%s world_fn=%zu world_xrefs=%zu chains=%zu\n",
                    record.slot_rva,
                    record.identity.identity_evidence_strength.c_str(),
                    record.identity.world_string_xref_functions,
                    record.identity.world_related_static_xrefs,
                    record.identity.chains.size());
        std::printf("    rationale: %s\n", record.identity.classification_rationale.c_str());
        for (const auto& chain : record.identity.chains) {
            std::printf("    chain slot_ref=0x%08X fn=0x%08X-0x%08X str_ref=0x%08X str=0x%08X %s\n",
                        chain.slot_ref_instruction_rva,
                        chain.function_begin_rva,
                        chain.function_end_rva,
                        chain.string_ref_instruction_rva,
                        chain.string_rva,
                        chain.string_label);
        }
    }
    if (result.identity.legacy_comparison_present) {
        const auto& legacy = result.identity.legacy_comparison;
        std::printf("\nLegacy identity comparison 0x%08X: %s strength=%s world_xrefs=%zu chains=%zu\n",
                    legacy.slot_rva,
                    deflux::game::gta_sa_de::to_string(legacy.identity.classification),
                    legacy.identity.identity_evidence_strength.c_str(),
                    legacy.identity.world_related_static_xrefs,
                    legacy.identity.chains.size());
        for (const auto& chain : legacy.identity.chains) {
            std::printf("  chain slot_ref=0x%08X fn=0x%08X str_ref=0x%08X str=0x%08X %s\n",
                        chain.slot_ref_instruction_rva,
                        chain.function_begin_rva,
                        chain.string_ref_instruction_rva,
                        chain.string_rva,
                        chain.string_label);
        }
    }
    const auto high_traffic = std::find_if(
        result.identity.gworld_like_records.begin(),
        result.identity.gworld_like_records.end(),
        [](const deflux::game::gta_sa_de::GWorldIdentityCandidateRecord& r) {
            return r.slot_rva == 0x05764130u;
        });
    if (high_traffic != result.identity.gworld_like_records.end()) {
        std::printf("\nHigh-traffic 0x05764130 identity: %s strength=%s world_fn=%zu negative_fn=%zu\n",
                    deflux::game::gta_sa_de::to_string(high_traffic->identity.classification),
                    high_traffic->identity.identity_evidence_strength.c_str(),
                    high_traffic->identity.world_string_xref_functions,
                    high_traffic->identity.negative_string_xref_functions);
    }
    std::printf("\nLegacy comparison records:\n");
    for (const auto& record : result.semantic.comparison_records) {
        std::printf("  slot=0x%08X class=%s ctx_refs=%zu object_refs=%zu regions=%zu rationale=%s\n",
                    record.slot_rva,
                    deflux::game::gta_sa_de::to_string(record.classification),
                    record.diversity.context_qualified_references,
                    record.object_usage.refs_with_any_object_usage,
                    record.diversity.unique_code_regions,
                    record.classification_rationale.c_str());
    }
    std::printf("\nCandidates:\n");
    for (const auto& candidate : result.candidates) {
        std::printf("  slot=0x%08X refs=%zu section=%s\n",
                    candidate.slot_rva,
                    candidate.reference_count,
                    candidate.dest_section);
        for (const auto& reason : candidate.survival_reasons) {
            std::printf("    reason: %s\n", reason.c_str());
        }
        for (const auto& ref : candidate.references) {
            std::printf("    instr=0x%08X reg=%s src=%s null_test=%d branch=%d\n",
                        ref.instruction_rva,
                        ref.dest_reg,
                        ref.source_section,
                        ref.has_null_test ? 1 : 0,
                        ref.has_null_branch ? 1 : 0);
        }
    }
}

void print_live_observation_report(
    const deflux::game::gta_sa_de::LiveObservationSessionResult& result) {
    std::printf("GWorld Live Candidate Observation (Task #7B.1)\n");
    std::printf("  status: %s\n", deflux::game::gta_sa_de::to_string(result.status));
    std::printf("  message: %s\n", result.message.c_str());
    std::printf("  pid: %u\n", result.process_id);
    std::printf("  module_base: 0x%llX\n", static_cast<unsigned long long>(result.module_base));
    std::printf("  module_size: 0x%llX\n", static_cast<unsigned long long>(result.module_size));
    std::printf("  sample_batches: %zu\n", result.samples_collected);
    std::printf("  raw_samples: %zu\n", result.samples.size());

    for (const auto& summary : result.summaries) {
        std::printf("\nCandidate 0x%08X summary:\n", summary.candidate_rva);
        std::printf("  samples=%zu read_ok=%zu null=%zu non_null=%zu distinct=%zu stable=%s consistency=%s\n",
                    summary.samples,
                    summary.read_successes,
                    summary.null_samples,
                    summary.non_null_samples,
                    summary.distinct_pointer_values,
                    summary.pointer_stable ? "yes" : "no",
                    deflux::game::gta_sa_de::to_string(summary.consistency));
        std::printf("  first_value_in_module_samples=%zu\n", summary.first_value_in_module_samples);
        for (const auto& phase : summary.by_phase) {
            std::printf("  phase=%s samples=%zu consistency=%s distinct=%zu\n",
                        deflux::game::gta_sa_de::to_string(phase.phase),
                        phase.samples,
                        deflux::game::gta_sa_de::to_string(phase.consistency),
                        phase.distinct_pointer_values);
        }
    }

    std::printf("\nSample log:\n");
    for (const auto& sample : result.samples) {
        std::printf("  idx=%u phase=%s rva=0x%08X slot_va=0x%llX read=%s value=0x%llX null=%s canonical=%s "
                    "obj_read=%s first=0x%llX first_in_module=%s\n",
                    sample.sample_index,
                    deflux::game::gta_sa_de::to_string(sample.phase),
                    sample.candidate_rva,
                    static_cast<unsigned long long>(sample.slot_virtual_address),
                    sample.slot_read_ok ? "yes" : "no",
                    static_cast<unsigned long long>(sample.slot_value),
                    sample.slot_is_null ? "yes" : "no",
                    sample.slot_is_canonical ? "yes" : "no",
                    sample.object_probe_read_ok ? "yes" : "no",
                    static_cast<unsigned long long>(sample.first_pointer_value),
                    sample.first_value_in_module ? "yes" : "no");
    }
}

void print_structural_identity_report(
    const deflux::game::gta_sa_de::StructuralIdentityProbeResult& result) {
    std::printf("GWorld Structural Identity Probe (Task #7B.2)\n");
    std::printf("  status: %s\n", deflux::game::gta_sa_de::to_string(result.status));
    std::printf("  message: %s\n", result.message.c_str());
    const auto& snap = result.snapshot;
    std::printf("  candidate_rva: 0x%08X\n", snap.candidate_rva);
    std::printf("  classification: %s\n", deflux::game::gta_sa_de::to_string(snap.classification));
    std::printf("  rationale: %s\n", snap.classification_rationale.c_str());
    std::printf("  slot_va: 0x%llX read=%s object_va=0x%llX null=%s\n",
                static_cast<unsigned long long>(snap.slot_virtual_address),
                snap.slot_read_ok ? "yes" : "no",
                static_cast<unsigned long long>(snap.object_virtual_address),
                snap.object_is_null ? "yes" : "no");
    std::printf("  prefix_read=%s bytes=%zu first_qword=0x%llX in_module=%s\n",
                snap.object_prefix_read_ok ? "yes" : "no",
                snap.object_prefix_bytes_read,
                static_cast<unsigned long long>(snap.first_qword),
                snap.first_qword_in_module ? "yes" : "no");
    for (const auto& note : snap.hypothesis_notes) {
        std::printf("  hypothesis: %s\n", note.c_str());
    }
    for (const auto& note : snap.correlation_notes) {
        std::printf("  correlation: %s\n", note.c_str());
    }
    const auto field_lines =
        deflux::game::gta_sa_de::format_structural_identity_field_lines(snap);
    std::printf("  fields: %zu\n", field_lines.size());
    for (const std::string& line : field_lines) {
        std::printf("    %s\n", line.c_str());
    }
}

int run_gworld_structural_probe() {
    std::printf("GWorld structural identity probe starting (read-only diagnostic)...\n");
    std::printf("This tool never launches GTA. Launch vanilla GTA manually via Steam if needed.\n");
    std::fflush(stdout);

    deflux::game::gta_sa_de::GtaSaDeAdapter adapter;
    const auto observation = adapter.observe();
    if (observation.state != deflux::core::ProcessLifecycle::ProcessDetected
        || !deflux::core::is_valid_process_instance(observation.instance)) {
        std::printf("\nGTA is not running with a verified process identity.\n");
        std::printf("STOP: launch vanilla GTA manually through Steam, then rerun:\n");
        std::printf("  DeFlux_TestClient.exe --gworld-structural-probe\n");
        return 0;
    }

    const auto result = deflux::game::gta_sa_de::run_structural_identity_probe(observation);
    print_structural_identity_report(result);
    return result.status == deflux::game::gta_sa_de::LiveObservationStatus::Ok ? 0 : 2;
}

void print_game_state_observation_report(
    const deflux::game::gta_sa_de::GameStateObservationSessionResult& result) {
    std::printf("\nGGameState observation report:\n");
    std::printf("  status: %s\n", deflux::game::gta_sa_de::to_string(result.status));
    std::printf("  message: %s\n", result.message.c_str());
    std::printf("  classification: %s\n",
                deflux::game::gta_sa_de::to_string(result.classification));
    std::printf("  rationale: %s\n", result.classification_rationale.c_str());
    std::printf("  derivation: %s resolved=%s global_rva=0x%08X cmp_rva=0x%08X anchor_rva=0x%08X\n",
                deflux::game::gta_sa_de::to_string(result.resolution.method),
                result.resolution.resolved ? "yes" : "no",
                result.resolution.global_rva,
                result.resolution.cmp_instruction_rva,
                result.resolution.anchor_string_rva);
    std::printf("  resolution_message: %s\n", result.resolution.message.c_str());
    std::printf("  samples: %zu\n", result.samples.size());
    for (const auto& sample : result.samples) {
        std::printf("    [%u] phase=%s read=%s value=%u va=0x%llX identity=%s\n",
                    sample.sample_index,
                    deflux::game::gta_sa_de::to_string(sample.phase),
                    sample.read_ok ? "yes" : "no",
                    static_cast<unsigned>(sample.raw_value),
                    static_cast<unsigned long long>(sample.global_va),
                    sample.process_identity_valid ? "valid" : "invalid");
    }
    std::printf("  by_phase: %zu\n", result.by_phase.size());
    for (const auto& summary : result.by_phase) {
        std::printf("    phase=%s samples=%zu successes=%zu distinct=%zu\n",
                    deflux::game::gta_sa_de::to_string(summary.phase),
                    summary.samples,
                    summary.read_successes,
                    summary.distinct_values);
    }
}

int run_ggame_state_observe_live(std::uint32_t max_samples, std::uint32_t interval_ms) {
    std::printf("GGameState live observation starting (read-only diagnostic)...\n");
    std::printf("This tool never launches GTA. Launch vanilla GTA manually via Steam if needed.\n");
    std::fflush(stdout);

    deflux::game::gta_sa_de::GtaSaDeAdapter adapter;
    const auto observation = adapter.observe();
    if (observation.state != deflux::core::ProcessLifecycle::ProcessDetected
        || !deflux::core::is_valid_process_instance(observation.instance)) {
        std::printf("\nGTA is not running with a verified process identity.\n");
        std::printf("STOP: launch vanilla GTA manually through Steam, then rerun:\n");
        std::printf("  DeFlux_TestClient.exe --ggame-state-observe-live\n");
        return 0;
    }

    deflux::game::gta_sa_de::GameStateObservationConfig config{};
    config.max_samples = max_samples;
    config.interval_ms = interval_ms;

    const auto result = deflux::game::gta_sa_de::run_game_state_observation_session(
        observation,
        config,
        [](deflux::game::gta_sa_de::LiveObservationPhase& phase_out) {
            std::printf("\nEnter phase label "
                        "(MENU/LOADING/GAMEPLAY_STANDING/GAMEPLAY_MOVING/PAUSE_MENU) or STOP: ");
            std::fflush(stdout);
            char line[128]{};
            if (std::fgets(line, sizeof(line), stdin) == nullptr) {
                return false;
            }
            std::string label(line);
            while (!label.empty()
                   && (label.back() == '\n' || label.back() == '\r' || label.back() == ' ')) {
                label.pop_back();
            }
            if (label == "STOP" || label == "stop" || label == "Stop") {
                return false;
            }
            phase_out = deflux::game::gta_sa_de::parse_live_observation_phase(label);
            if (phase_out == deflux::game::gta_sa_de::LiveObservationPhase::Unknown) {
                std::printf("Unknown phase label.\n");
            }
            return true;
        });

    print_game_state_observation_report(result);
    return result.status == deflux::game::gta_sa_de::LiveObservationStatus::Ok ? 0 : 2;
}

int run_gworld_observe_live(std::uint32_t max_samples, std::uint32_t interval_ms) {
    std::printf("GWorld live observation starting (read-only diagnostic)...\n");
    std::printf("This tool never launches GTA. Launch vanilla GTA manually via Steam if needed.\n");
    std::fflush(stdout);

    deflux::game::gta_sa_de::GtaSaDeAdapter adapter;
    const auto observation = adapter.observe();
    if (observation.state != deflux::core::ProcessLifecycle::ProcessDetected
        || !deflux::core::is_valid_process_instance(observation.instance)) {
        std::printf("\nGTA is not running with a verified process identity.\n");
        std::printf("STOP: launch vanilla GTA manually through Steam, then rerun:\n");
        std::printf("  DeFlux_TestClient.exe --gworld-observe-live\n");
        return 0;
    }

    deflux::game::gta_sa_de::LiveObservationConfig config{};
    config.max_samples = max_samples;
    config.interval_ms = interval_ms;

    const auto result = deflux::game::gta_sa_de::run_live_candidate_observation_session(
        observation,
        config,
        [](deflux::game::gta_sa_de::LiveObservationPhase& phase_out) {
            std::printf("\nEnter phase label "
                        "(MENU/LOADING/GAMEPLAY_STANDING/GAMEPLAY_MOVING/PAUSE_MENU) or STOP: ");
            std::fflush(stdout);
            char line[128]{};
            if (std::fgets(line, sizeof(line), stdin) == nullptr) {
                return false;
            }
            std::string label(line);
            while (!label.empty()
                   && (label.back() == '\n' || label.back() == '\r' || label.back() == ' ')) {
                label.pop_back();
            }
            if (label == "STOP" || label == "stop" || label == "Stop") {
                return false;
            }
            phase_out = deflux::game::gta_sa_de::parse_live_observation_phase(label);
            if (phase_out == deflux::game::gta_sa_de::LiveObservationPhase::Unknown) {
                std::printf("Unknown phase label.\n");
            }
            return true;
        });

    print_live_observation_report(result);
    return result.status == deflux::game::gta_sa_de::LiveObservationStatus::Ok ? 0 : 2;
}

void print_legacy_chain_diagnostic_report(
    const deflux::game::gta_sa_de::LegacyChainDiagnosticSessionResult& result) {
    std::printf("\nLegacy chain diagnostic report:\n");
    std::printf("  status: %s\n", deflux::game::gta_sa_de::to_string(result.status));
    std::printf("  message: %s\n", result.message.c_str());
    std::printf("  classification: %s\n",
                deflux::game::gta_sa_de::to_string(result.classification));
    std::printf("  rationale: %s\n", result.classification_rationale.c_str());
    std::printf("  samples: %zu\n", result.samples.size());
    for (const auto& sample : result.samples) {
        std::printf("    [%u] phase=%s identity=%s",
                    sample.sample_index,
                    deflux::game::gta_sa_de::to_string(sample.phase),
                    sample.process_identity_valid ? "valid" : "invalid");
        if (sample.game_state_sampled) {
            std::printf(" gGameState=%s raw=%u",
                        sample.game_state_read_ok ? "read_ok" : "read_fail",
                        static_cast<unsigned>(sample.raw_game_state));
        }
        std::printf("\n");
        for (const auto& profile : sample.profiles) {
            std::printf("      slot=0x%08X profile=%s stage=%s preserved=%s shape=%s",
                        profile.opaque_root_rva,
                        profile.profile_name != nullptr ? profile.profile_name : "(null)",
                        deflux::game::gta_sa_de::to_string(profile.stage),
                        profile.local_player_candidate_preserved ? "yes" : "no",
                        profile.transform.shape_valid ? "TRANSFORM_SHAPE_VALID" : "no");
            if (profile.transform.shape_valid) {
                std::printf(" pos=(%.2f, %.2f, %.2f) delta=%.2f changed=%s",
                            profile.transform.translation.x,
                            profile.transform.translation.y,
                            profile.transform.translation.z,
                            profile.movement.distance,
                            profile.movement.changed_beyond_epsilon ? "yes" : "no");
            }
            std::printf("\n");
        }
    }
}

int run_legacy_chain_diagnose_live(std::uint32_t max_samples, std::uint32_t interval_ms) {
    std::printf("Legacy chain diagnostic starting (read-only research)...\n");
    std::printf("Opaque roots only. Not GWorld qualification. Not local-player identity.\n");
    std::printf("This tool never launches GTA. Launch vanilla GTA manually via Steam if needed.\n");
    std::fflush(stdout);

    deflux::game::gta_sa_de::GtaSaDeAdapter adapter;
    const auto observation = adapter.observe();
    if (observation.state != deflux::core::ProcessLifecycle::ProcessDetected
        || !deflux::core::is_valid_process_instance(observation.instance)) {
        std::printf("\nGTA is not running with a verified process identity.\n");
        std::printf("STOP: launch vanilla GTA manually through Steam, then rerun:\n");
        std::printf("  DeFlux_TestClient.exe --legacy-chain-diagnose-live\n");
        return 0;
    }

    deflux::game::gta_sa_de::LegacyChainDiagnosticConfig config{};
    config.max_samples = max_samples;
    config.interval_ms = interval_ms;

    const auto result = deflux::game::gta_sa_de::run_legacy_chain_diagnostic_session(
        observation,
        config,
        [](deflux::game::gta_sa_de::LiveObservationPhase& phase_out) {
            std::printf("\nEnter phase label "
                        "(MENU/LOADING/GAMEPLAY_STANDING/GAMEPLAY_MOVING/PAUSE_MENU) or STOP: ");
            std::fflush(stdout);
            char line[128]{};
            if (std::fgets(line, sizeof(line), stdin) == nullptr) {
                return false;
            }
            std::string label(line);
            while (!label.empty()
                   && (label.back() == '\n' || label.back() == '\r' || label.back() == ' ')) {
                label.pop_back();
            }
            if (label == "STOP" || label == "stop" || label == "Stop") {
                return false;
            }
            phase_out = deflux::game::gta_sa_de::parse_live_observation_phase(label);
            if (phase_out == deflux::game::gta_sa_de::LiveObservationPhase::Unknown) {
                std::printf("Unknown phase label.\n");
            }
            return true;
        });

    print_legacy_chain_diagnostic_report(result);
    return result.status == deflux::game::gta_sa_de::LiveObservationStatus::Ok ? 0 : 2;
}

int run_gworld_research_offline(const char* path) {
    std::printf("GWorld research offline starting...\n");
    std::fflush(stdout);
    constexpr const char* kExpectedSha =
        "63e633bb51c35fe35ca98f503b62e7ddb8403abe4c9d501cfa9162892453846f";
    std::wstring wide_path(path, path + std::strlen(path));
    const auto result =
        deflux::game::gta_sa_de::run_gworld_discovery_research_file(wide_path, kExpectedSha);
    std::printf("GWorld research scan complete.\n");
    std::fflush(stdout);
    print_gworld_research_report(result);
    return result.sha_verified ? 0 : 2;
}

int run_local_player_semantic_offline(const char* path) {
    std::printf("Local player semantic research offline starting...\n");
    std::fflush(stdout);
    constexpr const char* kExpectedSha =
        deflux::game::gta_sa_de::kLocalPlayerSemanticTargetSha256;
    std::wstring wide_path(path, path + std::strlen(path));
    const auto summary =
        deflux::game::gta_sa_de::run_local_player_semantic_research_file(wide_path, kExpectedSha);
    const auto lines = deflux::game::gta_sa_de::format_local_player_semantic_report(summary);
    for (const std::string& line : lines) {
        std::printf("%s\n", line.c_str());
    }
    std::fflush(stdout);
    return summary.sha_verified ? 0 : 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--gworld-structural-probe") == 0) {
        return run_gworld_structural_probe();
    }

    if (argc > 1 && std::strcmp(argv[1], "--ggame-state-observe-live") == 0) {
        std::uint32_t max_samples =
            deflux::game::gta_sa_de::kGameStateObservationDefaultMaxSamples;
        std::uint32_t interval_ms =
            deflux::game::gta_sa_de::kGameStateObservationDefaultIntervalMs;
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--max-samples") == 0 && i + 1 < argc) {
                max_samples = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            } else if (std::strcmp(argv[i], "--interval-ms") == 0 && i + 1 < argc) {
                interval_ms = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            }
        }
        return run_ggame_state_observe_live(max_samples, interval_ms);
    }

    if (argc > 1 && std::strcmp(argv[1], "--legacy-chain-diagnose-live") == 0) {
        std::uint32_t max_samples = deflux::game::gta_sa_de::kLegacyChainDefaultMaxSamples;
        std::uint32_t interval_ms = deflux::game::gta_sa_de::kLegacyChainDefaultIntervalMs;
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--max-samples") == 0 && i + 1 < argc) {
                max_samples = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            } else if (std::strcmp(argv[i], "--interval-ms") == 0 && i + 1 < argc) {
                interval_ms = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            }
        }
        return run_legacy_chain_diagnose_live(max_samples, interval_ms);
    }

    if (argc > 1 && std::strcmp(argv[1], "--gworld-observe-live") == 0) {
        std::uint32_t max_samples = deflux::game::gta_sa_de::kLiveObservationDefaultMaxSamples;
        std::uint32_t interval_ms = deflux::game::gta_sa_de::kLiveObservationDefaultIntervalMs;
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--max-samples") == 0 && i + 1 < argc) {
                max_samples = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            } else if (std::strcmp(argv[i], "--interval-ms") == 0 && i + 1 < argc) {
                interval_ms = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            }
        }
        return run_gworld_observe_live(max_samples, interval_ms);
    }

    if (argc > 1 && std::strcmp(argv[1], "--gworld-research-offline") == 0) {
        const char* path =
            (argc > 2) ? argv[2]
                       : "C:\\Program Files (x86)\\Steam\\steamapps\\common\\GTA San Andreas - The Definitive "
                         "Edition\\Gameface\\Binaries\\Win64\\SanAndreas.exe";
        return run_gworld_research_offline(path);
    }

    if (argc > 1 && std::strcmp(argv[1], "--local-player-semantic-offline") == 0) {
        const char* path =
            (argc > 2) ? argv[2]
                       : "C:\\Program Files (x86)\\Steam\\steamapps\\common\\GTA San Andreas - The Definitive "
                         "Edition\\Gameface\\Binaries\\Win64\\SanAndreas.exe";
        return run_local_player_semantic_offline(path);
    }

    const bool investigate = argc > 1 && std::strcmp(argv[1], "--qualify-investigate") == 0;

    std::printf("DeFlux Test Client\n");
    std::printf("Headless M1 host. Passive detection, observation, and read-only qualification.\n");
    if (investigate) {
        std::printf("Developer mode: --qualify-investigate\n");
    }
    std::printf("\n");

    print_version("DeFlux version", deflux::version::kApplication);
    print_version("Protocol version", deflux::version::kProtocol);
    print_version("Game Adapter API version", deflux::version::kGameAdapterApi);
    std::printf("Runtime ABI version: %u\n", deflux::version::kRuntimeAbi);

    std::printf("\nSession states (multiplayer; not process observation):\n");
    const deflux::core::SessionState states[] = {
        deflux::core::SessionState::Disconnected,
        deflux::core::SessionState::Connecting,
        deflux::core::SessionState::Handshaking,
        deflux::core::SessionState::Connected,
        deflux::core::SessionState::WorldLoading,
        deflux::core::SessionState::Ready,
        deflux::core::SessionState::Spawning,
        deflux::core::SessionState::Playing,
    };
    for (const auto state : states) {
        std::printf("  %s\n", deflux::core::to_string(state));
    }

    deflux::game::gta_sa_de::GtaSaDeAdapter adapter;
    const auto identity = adapter.identity();
    char api_text[32]{};
    (void)deflux::version::format(adapter.api_version(), api_text, sizeof(api_text));

    std::printf("\nGTA SA DE adapter:\n");
    std::printf("  id: %s\n", identity.id != nullptr ? identity.id : "");
    std::printf("  display_name: %s\n", identity.display_name != nullptr ? identity.display_name : "");
    std::printf("  api_version: %s\n", api_text);

    const auto detection = adapter.detect();
    std::printf("\nGTA SA DE Detection:\n");
    std::printf("  installation: %s\n", deflux::game::gta_sa_de::to_string(detection.installation));
    std::printf("  executable: %s\n",
                detection.executable_path.empty() ? "(none)" : detection.executable_path.c_str());
    std::printf("  running: %s\n", detection.running ? "yes" : "no");
    if (detection.running) {
        std::printf("  pid: %u\n", detection.process_id);
    } else {
        std::printf("  pid: (none)\n");
    }
    std::printf("  sha256: %s\n",
                detection.sha256_hex.empty() ? "(none)" : detection.sha256_hex.c_str());
    if (detection.file_size_bytes > 0) {
        std::printf("  size: %llu\n", static_cast<unsigned long long>(detection.file_size_bytes));
    }
    if (!detection.file_version.empty()) {
        std::printf("  file_version: %s\n", detection.file_version.c_str());
    }
    if (!detection.product_version.empty()) {
        std::printf("  product_version: %s\n", detection.product_version.c_str());
    }
    std::printf("  compatibility: %s\n", deflux::game::gta_sa_de::to_string(detection.compatibility));
    std::printf("  message: %s\n", detection.message.c_str());

    const auto runtime = adapter.observe();
    std::printf("\nRuntime Observation:\n");
    std::printf("  state: %s\n", deflux::core::to_string(runtime.state));
    if (runtime.instance.process_id == 0) {
        std::printf("  pid: none\n");
        std::printf("  process_instance: none\n");
    } else {
        std::printf("  pid: %u\n", runtime.instance.process_id);
        std::printf("  executable: %s\n", runtime.instance.executable_path.c_str());
        std::printf("  creation_time: %llu\n",
                    static_cast<unsigned long long>(runtime.instance.creation_identity));
        std::printf("  instance_changed: %s\n", runtime.instance_changed ? "yes" : "no");
    }
    std::printf("  message: %s\n", runtime.message.c_str());

    if (investigate) {
        print_investigation(adapter.investigate());
    } else {
        print_qualification(adapter.qualify());
    }

    std::printf("\nReadiness (not implemented; a qualified GWorld slot is not readiness):\n");
    print_readiness("engine_ready", adapter.engine_ready());
    print_readiness("world_ready", adapter.world_ready());
    print_readiness("local_player_ready", adapter.local_player_ready());

    std::printf("\nProcess presence is not compatibility, attachment, or readiness.\n");
    return 0;
}
