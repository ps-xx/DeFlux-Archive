#include "deflux/game/gta_sa_de/gworld_live_observation.hpp"

#include "deflux/game/gta_sa_de/qualification.hpp"
#include "environment_preflight.hpp"
#include "fingerprint.hpp"
#include "module_query.hpp"
#include "observation.hpp"
#include "remote_read.hpp"
#include "win_utf8.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace deflux::game::gta_sa_de {
namespace {

StructuralIdentityProbeResult make_probe_status(LiveObservationStatus status, std::string message) {
    StructuralIdentityProbeResult result{};
    result.status = status;
    result.message = std::move(message);
    return result;
}

void append_hypothesis_note(StructuralIdentitySnapshot& snapshot, const char* text) {
    if (text != nullptr && text[0] != '\0') {
        snapshot.hypothesis_notes.emplace_back(text);
    }
}

void append_correlation_notes(StructuralIdentitySnapshot& snapshot,
                              const LiveObservationProbeContext& context) {
    std::size_t module_resident_fields = 0;
    std::size_t canonical_fields = 0;
    for (const StructuralIdentityFieldProbe& field : snapshot.fields) {
        if (field.is_canonical && !field.is_null) {
            ++canonical_fields;
        }
        if (field.in_loaded_module) {
            ++module_resident_fields;
        }
    }

    if (snapshot.first_qword_in_module) {
        append_hypothesis_note(
            snapshot,
            "HYPOTHESIS_ONLY: first qword points into a loaded module (vtable-like pattern).");
    }
    if (canonical_fields > 0) {
        snapshot.correlation_notes.push_back(
            "Offline Task #7A.3 identity evidence for this RVA was WEAK/MIXED/INSUFFICIENT and "
            "does not qualify the slot as GWorld.");
    }
    snapshot.correlation_notes.push_back(
        "Bounded prefix captured " + std::to_string(snapshot.object_prefix_bytes_read)
        + " bytes with " + std::to_string(module_resident_fields)
        + " module-resident pointer field(s) out of "
        + std::to_string(snapshot.fields.size()) + ".");
}

bool verify_identity_on_handle(const deflux::core::ProcessInstance& expected,
                               detail::ScopedProcess& process,
                               std::string& message_out) {
    std::uint32_t live_pid = 0;
    std::uint64_t live_created = 0;
    std::wstring live_path;
    if (!process.query_identity(live_pid, live_created, live_path)) {
        message_out = "Could not re-query process identity.";
        return false;
    }

    deflux::core::ProcessInstance live{};
    live.process_id = live_pid;
    live.creation_identity = live_created;
    live.executable_path = detail::wide_to_utf8(live_path);
    if (!process_identity_matches(expected, live)) {
        message_out = "Process identity changed during structural probing; aborting.";
        return false;
    }
    return true;
}

} // namespace

bool is_structural_identity_probe_candidate(std::uint32_t candidate_rva) noexcept {
    return candidate_rva == kStructuralIdentityProbeCandidateRva;
}

const char* to_string(StructureIdentityClassification classification) noexcept {
    switch (classification) {
    case StructureIdentityClassification::Observed:
        return "STRUCTURE_OBSERVED";
    case StructureIdentityClassification::Insufficient:
        return "STRUCTURE_INSUFFICIENT";
    case StructureIdentityClassification::Contradicted:
        return "STRUCTURE_CONTRADICTED";
    }
    return "UNKNOWN";
}

StructureIdentityClassification classify_structure_identity(
    const StructuralIdentitySnapshot& snapshot) noexcept {
    if (!is_structural_identity_probe_candidate(snapshot.candidate_rva)) {
        return StructureIdentityClassification::Insufficient;
    }
    if (!snapshot.slot_read_ok) {
        return StructureIdentityClassification::Insufficient;
    }
    if (snapshot.object_is_null) {
        return StructureIdentityClassification::Insufficient;
    }
    if (snapshot.object_prefix_bytes_read < kStructuralPointerFieldSize) {
        return StructureIdentityClassification::Insufficient;
    }

    if (!snapshot.first_qword_in_module && snapshot.first_qword != 0
        && is_canonical_user_pointer(snapshot.first_qword)) {
        return StructureIdentityClassification::Contradicted;
    }
    if (snapshot.first_qword != 0 && !is_canonical_user_pointer(snapshot.first_qword)) {
        return StructureIdentityClassification::Contradicted;
    }
    if (!snapshot.object_prefix_read_ok) {
        return StructureIdentityClassification::Insufficient;
    }
    if (snapshot.first_qword_in_module && snapshot.first_qword != 0) {
        return StructureIdentityClassification::Observed;
    }
    return StructureIdentityClassification::Insufficient;
}

StructuralIdentitySnapshot probe_candidate_structure(
    std::uint32_t candidate_rva,
    const LiveObservationProbeContext& context,
    const LiveObservationReadFn& read) noexcept {
    StructuralIdentitySnapshot snapshot{};
    snapshot.candidate_rva = candidate_rva;

    if (!is_structural_identity_probe_candidate(candidate_rva) || context.module_base == 0
        || context.module_size < 8 || !read) {
        snapshot.classification = StructureIdentityClassification::Insufficient;
        snapshot.classification_rationale =
            "RVA is not approved for structural identity probing or probe context is invalid.";
        return snapshot;
    }

    const std::uint64_t slot_va = context.module_base + static_cast<std::uint64_t>(candidate_rva);
    if (slot_va + 8 > context.module_base + context.module_size) {
        snapshot.classification = StructureIdentityClassification::Insufficient;
        snapshot.classification_rationale = "Candidate slot virtual address is outside the main module.";
        return snapshot;
    }

    snapshot.slot_virtual_address = slot_va;
    std::uint64_t object_va = 0;
    snapshot.slot_read_ok = read(slot_va, &object_va, sizeof(object_va));
    snapshot.object_virtual_address = object_va;
    snapshot.object_is_null = snapshot.slot_read_ok && object_va == 0;

    if (!snapshot.slot_read_ok || snapshot.object_is_null) {
        snapshot.classification = StructureIdentityClassification::Insufficient;
        snapshot.classification_rationale = snapshot.object_is_null
            ? "Candidate slot is null; structural prefix cannot be captured."
            : "Candidate slot read failed.";
        return snapshot;
    }

    if (pointer_in_range(object_va, context.module_base, context.module_size)) {
        snapshot.classification = StructureIdentityClassification::Contradicted;
        snapshot.classification_rationale =
            "Candidate object address lies inside the main module image (not a heap object probe target).";
        append_hypothesis_note(snapshot,
                             "HYPOTHESIS_ONLY: UObject-like globals typically reference heap objects.");
        return snapshot;
    }

    std::array<std::uint8_t, kStructuralObjectPrefixBytes> prefix{};
    snapshot.object_prefix_read_ok =
        read(object_va, prefix.data(), prefix.size());
    snapshot.object_prefix_bytes_read =
        snapshot.object_prefix_read_ok ? kStructuralObjectPrefixBytes : 0;

    if (!snapshot.object_prefix_read_ok) {
        snapshot.classification = StructureIdentityClassification::Insufficient;
        snapshot.classification_rationale =
            "Failed to read the bounded object prefix (256 bytes max).";
        return snapshot;
    }

    std::memcpy(&snapshot.first_qword, prefix.data(), sizeof(snapshot.first_qword));
    snapshot.first_qword_in_module = address_in_loaded_module(
        snapshot.first_qword, context.module_ranges, snapshot.first_qword_module_base);

    snapshot.fields.reserve(kStructuralMaxPointerFields);
    for (std::size_t offset = 0; offset + kStructuralPointerFieldSize <= kStructuralObjectPrefixBytes;
         offset += kStructuralPointerFieldSize) {
        StructuralIdentityFieldProbe field{};
        field.offset = static_cast<std::uint32_t>(offset);
        std::memcpy(&field.raw_value, prefix.data() + offset, sizeof(field.raw_value));
        field.is_null = field.raw_value == 0;
        field.is_canonical = !field.is_null && is_canonical_user_pointer(field.raw_value);
        field.in_loaded_module =
            address_in_loaded_module(field.raw_value, context.module_ranges, field.module_base);

        if (field.is_canonical) {
            field.dereference_attempted = true;
            field.target_readable =
                read(field.raw_value, &field.dereferenced_value, sizeof(field.dereferenced_value));
        }

        snapshot.fields.push_back(field);
    }

    append_correlation_notes(snapshot, context);
    snapshot.classification = classify_structure_identity(snapshot);
    switch (snapshot.classification) {
    case StructureIdentityClassification::Observed:
        snapshot.classification_rationale =
            "Bounded prefix captured with a module-resident first qword (vtable-like pattern).";
        break;
    case StructureIdentityClassification::Contradicted:
        if (snapshot.classification_rationale.empty()) {
            snapshot.classification_rationale =
                "Bounded prefix contradicts a UWorld-like UObject identity pattern.";
        }
        break;
    case StructureIdentityClassification::Insufficient:
        if (snapshot.classification_rationale.empty()) {
            snapshot.classification_rationale =
                "Insufficient bounded structural evidence for identity correlation.";
        }
        break;
    }
    return snapshot;
}

std::vector<std::string> format_structural_identity_field_lines(
    const StructuralIdentitySnapshot& snapshot) {
    std::vector<std::string> lines;
    lines.reserve(snapshot.fields.size());
    for (const StructuralIdentityFieldProbe& field : snapshot.fields) {
        char buffer[256]{};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "off=0x%X raw=0x%llX canonical=%s in_module=%s deref=%s target_read=%s deref_val=0x%llX",
            field.offset,
            static_cast<unsigned long long>(field.raw_value),
            field.is_canonical ? "yes" : "no",
            field.in_loaded_module ? "yes" : "no",
            field.dereference_attempted ? "yes" : "no",
            field.target_readable ? "yes" : "no",
            static_cast<unsigned long long>(field.dereferenced_value));
        lines.emplace_back(buffer);
    }
    return lines;
}

StructuralIdentityProbeResult run_structural_identity_probe(
    const deflux::core::ProcessObservation& observation) {
    if (observation.state == deflux::core::ProcessLifecycle::ObservationError) {
        return make_probe_status(LiveObservationStatus::Error,
                                 observation.message.empty()
                                     ? std::string("Process observation error.")
                                     : observation.message);
    }
    if (observation.state != deflux::core::ProcessLifecycle::ProcessDetected
        || !deflux::core::is_valid_process_instance(observation.instance)) {
        return make_probe_status(LiveObservationStatus::NotRunning,
                                 "No verified GTA process is running.");
    }

    const EnvironmentPreflightResult preflight =
        scan_game_executable_directory(observation.instance.executable_path);
    if (!preflight.clean) {
        auto result = make_probe_status(LiveObservationStatus::EnvironmentContaminated, preflight.message);
        return result;
    }

    const std::wstring path = detail::utf8_to_wide(observation.instance.executable_path);
    const auto fingerprint = detail::fingerprint_executable(path);
    if (!fingerprint.ok || !sha256_matches_live_observation_target(fingerprint.sha256_hex)) {
        return make_probe_status(LiveObservationStatus::BuildMismatch,
                                 "Executable SHA-256 does not match the structural probe target build.");
    }

    detail::ScopedProcess process;
    if (!process.open_read_only(observation.instance.process_id)) {
        return make_probe_status(LiveObservationStatus::Error,
                                 "OpenProcess(QUERY_LIMITED|VM_READ) failed.");
    }

    std::string identity_message;
    if (!verify_identity_on_handle(observation.instance, process, identity_message)) {
        return make_probe_status(LiveObservationStatus::ProcessChanged, identity_message);
    }

    const auto module = detail::query_main_module(observation.instance.process_id, path);
    if (!module.found || module.base == 0 || module.size == 0) {
        return make_probe_status(LiveObservationStatus::ModuleInvalid,
                                 module.message.empty() ? std::string("Main module discovery failed.")
                                                        : module.message);
    }

    std::vector<std::uint8_t> headers(0x400, 0);
    if (!process.read_exact(module.base, headers.data(), headers.size())) {
        return make_probe_status(LiveObservationStatus::ModuleInvalid,
                                 "Failed to read PE headers from the main module.");
    }

    PeImageInfo pe{};
    PeParseStatus pe_status = parse_pe64_headers(headers.data(), headers.size(), pe);
    if (pe_status == PeParseStatus::Ok && pe.size_of_headers > headers.size() && pe.size_of_headers <= 0x2000u) {
        headers.assign(pe.size_of_headers, 0);
        if (!process.read_exact(module.base, headers.data(), headers.size())) {
            return make_probe_status(LiveObservationStatus::ModuleInvalid,
                                     "Failed to read full PE headers from the main module.");
        }
        pe_status = parse_pe64_headers(headers.data(), headers.size(), pe);
    }
    if (pe_status != PeParseStatus::Ok || pe.size_of_image != static_cast<std::uint32_t>(module.size)) {
        return make_probe_status(LiveObservationStatus::ModuleInvalid,
                                 "PE validation failed for structural identity probing.");
    }

    if (kStructuralIdentityProbeCandidateRva >= pe.size_of_image
        || static_cast<std::uint64_t>(kStructuralIdentityProbeCandidateRva) + 8ull > module.size) {
        return make_probe_status(LiveObservationStatus::ModuleInvalid,
                                 "Structural probe candidate RVA is outside the validated image.");
    }
    if (!pe_rva_in_non_executable_section(headers.data(), headers.size(), kStructuralIdentityProbeCandidateRva)) {
        return make_probe_status(LiveObservationStatus::ModuleInvalid,
                                 "Structural probe candidate RVA is not in a non-executable section.");
    }

    std::vector<detail::ModuleRange> module_ranges;
    if (!detail::query_loaded_module_ranges(observation.instance.process_id, module_ranges)) {
        return make_probe_status(LiveObservationStatus::ModuleInvalid,
                                 "Failed to enumerate loaded module ranges.");
    }

    LiveObservationProbeContext context{};
    context.module_base = module.base;
    context.module_size = module.size;
    context.module_ranges.reserve(module_ranges.size());
    for (const detail::ModuleRange& range : module_ranges) {
        context.module_ranges.push_back(LiveObservationModuleRange{range.base, range.size});
    }

    const LiveObservationReadFn read_fn = [&process](std::uint64_t address, void* out, std::size_t bytes) {
        return process.read_exact(address, out, bytes);
    };

    if (!verify_identity_on_handle(observation.instance, process, identity_message)) {
        return make_probe_status(LiveObservationStatus::ProcessChanged, identity_message);
    }

    StructuralIdentityProbeResult result{};
    result.status = LiveObservationStatus::Ok;
    result.message = "Structural identity probe completed.";
    result.snapshot =
        probe_candidate_structure(kStructuralIdentityProbeCandidateRva, context, read_fn);

    if (!verify_identity_on_handle(observation.instance, process, identity_message)) {
        return make_probe_status(LiveObservationStatus::ProcessChanged, identity_message);
    }

    process.close();
    return result;
}

} // namespace deflux::game::gta_sa_de
