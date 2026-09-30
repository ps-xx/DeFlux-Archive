#include "deflux/game/gta_sa_de/qualification.hpp"

#include "environment_preflight.hpp"
#include "fingerprint.hpp"
#include "module_query.hpp"
#include "observation.hpp"
#include "remote_read.hpp"
#include "win_utf8.hpp"

#include "deflux/core/process_lifecycle.hpp"

#include <vector>

namespace deflux::game::gta_sa_de {
namespace {

// Qualification metadata only. This does NOT register the public supported-build
// table. RVA 0x05391B70 is a LEGACY_CANDIDATE / UNVERIFIED slot from legacy
// ue4_memory.cpp kCandidateRvas[0]. Task #6 live validation was
// INVALID_FOR_VANILLA_VALIDATION (legacy DeFluxCore active). Do not treat this
// as a trusted current-build GWorld locator. kCandidateRvas[1] (0x0FE1D1D8) is
// rejected because it exceeds this executable's size.
constexpr QualificationProfile kProfiles[] = {
    {
        "63e633bb51c35fe35ca98f503b62e7ddb8403abe4c9d501cfa9162892453846f",
        "1.0.113.21181",
        89497064ull,
        0x05391B70u,
        "legacy_gworld_slot_candidate",
        GWorldAnchorTrust::LegacyCandidateUnverified,
    },
};

std::string lower_copy(std::string_view text) {
    std::string out(text);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

const char* status_message(QualificationStatus status) {
    switch (status) {
    case QualificationStatus::NotRunning:
        return "No verified GTA process is running.";
    case QualificationStatus::EnvironmentContaminated:
        return "Legacy runtime contamination detected in the GTA executable directory.";
    case QualificationStatus::BuildMismatch:
        return "On-disk fingerprint does not match the qualification profile.";
    case QualificationStatus::ProcessChanged:
        return "Process identity changed during qualification; aborting.";
    case QualificationStatus::ModuleInvalid:
        return "Main module or PE headers failed validation.";
    case QualificationStatus::ProfileMissing:
        return "This executable fingerprint has no runtime qualification profile.";
    case QualificationStatus::AnchorNotFound:
        return "The GWorld slot was not present or was null.";
    case QualificationStatus::AnchorInvalid:
        return "The GWorld slot pointer failed conservative validation.";
    case QualificationStatus::Qualified:
        return "Runtime qualification checks passed for the GWorld slot anchor.";
    case QualificationStatus::Error:
        return "Qualification failed due to an inspection error.";
    }
    return "Unknown qualification status.";
}

} // namespace

const char* to_string(QualificationStatus status) noexcept {
    switch (status) {
    case QualificationStatus::NotRunning:
        return "NOT_RUNNING";
    case QualificationStatus::EnvironmentContaminated:
        return "ENVIRONMENT_CONTAMINATED";
    case QualificationStatus::BuildMismatch:
        return "BUILD_MISMATCH";
    case QualificationStatus::ProcessChanged:
        return "PROCESS_CHANGED";
    case QualificationStatus::ModuleInvalid:
        return "MODULE_INVALID";
    case QualificationStatus::ProfileMissing:
        return "PROFILE_MISSING";
    case QualificationStatus::AnchorNotFound:
        return "ANCHOR_NOT_FOUND";
    case QualificationStatus::AnchorInvalid:
        return "ANCHOR_INVALID";
    case QualificationStatus::Qualified:
        return "QUALIFIED";
    case QualificationStatus::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

const char* to_string(GWorldAnchorTrust trust) noexcept {
    switch (trust) {
    case GWorldAnchorTrust::LegacyCandidateUnverified:
        return "LEGACY_CANDIDATE_UNVERIFIED";
    }
    return "UNKNOWN";
}

EnvironmentPreflightResult scan_game_executable_directory(std::string_view executable_utf8_path) {
    return detail::scan_game_executable_directory(executable_utf8_path);
}

QualificationResult result_from_environment_contamination(const EnvironmentPreflightResult& preflight) {
    QualificationResult result{};
    result.status = QualificationStatus::EnvironmentContaminated;
    result.message = preflight.message;
    return result;
}

const char* to_string(PeParseStatus status) noexcept {
    switch (status) {
    case PeParseStatus::Ok:
        return "OK";
    case PeParseStatus::InvalidArgument:
        return "INVALID_ARGUMENT";
    case PeParseStatus::TooSmall:
        return "TOO_SMALL";
    case PeParseStatus::BadDosMagic:
        return "BAD_DOS_MAGIC";
    case PeParseStatus::BadNewHeaderOffset:
        return "BAD_NEW_HEADER_OFFSET";
    case PeParseStatus::BadNtSignature:
        return "BAD_NT_SIGNATURE";
    case PeParseStatus::NotAmd64:
        return "NOT_AMD64";
    case PeParseStatus::NotPe32Plus:
        return "NOT_PE32PLUS";
    case PeParseStatus::ImplausibleImageSize:
        return "IMPLAUSIBLE_IMAGE_SIZE";
    }
    return "UNKNOWN";
}

const char* to_string(AnchorCheck check) noexcept {
    switch (check) {
    case AnchorCheck::Unread:
        return "UNREAD";
    case AnchorCheck::SlotUnreadable:
        return "SLOT_UNREADABLE";
    case AnchorCheck::Null:
        return "NULL";
    case AnchorCheck::NonCanonical:
        return "NON_CANONICAL";
    case AnchorCheck::Unaligned:
        return "UNALIGNED";
    case AnchorCheck::InsideModule:
        return "INSIDE_MODULE";
    case AnchorCheck::ObjectUnreadable:
        return "OBJECT_UNREADABLE";
    case AnchorCheck::VtableNonCanonical:
        return "VTABLE_NON_CANONICAL";
    case AnchorCheck::VtableOutsideModule:
        return "VTABLE_OUTSIDE_MODULE";
    case AnchorCheck::Ok:
        return "OK";
    }
    return "UNKNOWN";
}

bool is_canonical_user_pointer(std::uint64_t pointer) noexcept {
    return pointer >= 0x10000ull && pointer <= 0x00007FFFFFFFFFFFull;
}

bool is_aligned_pointer(std::uint64_t pointer) noexcept {
    return (pointer & 7ull) == 0;
}

bool pointer_in_range(std::uint64_t pointer, std::uint64_t base, std::uint64_t size) noexcept {
    if (base == 0 || size == 0) {
        return false;
    }
    return pointer >= base && pointer < base + size;
}

bool complete_read_ok(std::size_t requested, std::size_t copied, bool api_success) noexcept {
    return api_success && requested != 0 && copied == requested;
}

const QualificationProfile* find_qualification_profile(std::string_view sha256_hex) noexcept {
    const std::string lowered = lower_copy(sha256_hex);
    for (const QualificationProfile& profile : kProfiles) {
        if (lowered == profile.sha256_hex) {
            return &profile;
        }
    }
    return nullptr;
}

bool fingerprint_matches_profile(const QualificationProfile& profile,
                                 std::string_view sha256_hex,
                                 std::uint64_t size_bytes,
                                 std::string_view file_version) noexcept {
    if (lower_copy(sha256_hex) != profile.sha256_hex) {
        return false;
    }
    if (profile.size_bytes != 0 && size_bytes != profile.size_bytes) {
        return false;
    }
    if (profile.file_version != nullptr && profile.file_version[0] != '\0') {
        if (file_version != profile.file_version) {
            return false;
        }
    }
    return profile.gworld_slot_rva != 0;
}

AnchorCheck evaluate_gworld_anchor(bool slot_read_ok,
                                   std::uint64_t slot_pointer,
                                   bool object_read_ok,
                                   std::uint64_t vtable_pointer,
                                   std::uint64_t module_base,
                                   std::uint64_t module_size) noexcept {
    if (!slot_read_ok) {
        return AnchorCheck::SlotUnreadable;
    }
    if (slot_pointer == 0) {
        return AnchorCheck::Null;
    }
    if (!is_aligned_pointer(slot_pointer)) {
        return AnchorCheck::Unaligned;
    }
    if (!is_canonical_user_pointer(slot_pointer)) {
        return AnchorCheck::NonCanonical;
    }
    if (pointer_in_range(slot_pointer, module_base, module_size)) {
        return AnchorCheck::InsideModule;
    }
    if (!object_read_ok) {
        return AnchorCheck::ObjectUnreadable;
    }
    if (!is_aligned_pointer(vtable_pointer) || !is_canonical_user_pointer(vtable_pointer)) {
        return AnchorCheck::VtableNonCanonical;
    }
    if (!pointer_in_range(vtable_pointer, module_base, module_size)) {
        return AnchorCheck::VtableOutsideModule;
    }
    return AnchorCheck::Ok;
}

QualificationStatus status_from_checks(const QualificationChecks& checks) noexcept {
    if (!checks.running) {
        return QualificationStatus::NotRunning;
    }
    if (!checks.identity_ok) {
        return QualificationStatus::Error;
    }
    if (!checks.identity_stable) {
        return QualificationStatus::ProcessChanged;
    }
    if (!checks.fingerprint_ok) {
        return QualificationStatus::Error;
    }
    if (!checks.profile_found) {
        return QualificationStatus::ProfileMissing;
    }
    if (!checks.fingerprint_matches_profile) {
        return QualificationStatus::BuildMismatch;
    }
    if (!checks.inspect_ok) {
        return QualificationStatus::Error;
    }
    if (!checks.module_ok || !checks.pe_ok) {
        return QualificationStatus::ModuleInvalid;
    }
    if (!checks.rva_in_image) {
        return QualificationStatus::AnchorNotFound;
    }
    if (!checks.rva_in_data_section) {
        return QualificationStatus::AnchorInvalid;
    }
    switch (checks.anchor) {
    case AnchorCheck::Ok:
        return QualificationStatus::Qualified;
    case AnchorCheck::Null:
    case AnchorCheck::Unread:
        return QualificationStatus::AnchorNotFound;
    default:
        return QualificationStatus::AnchorInvalid;
    }
}

QualificationResult result_from_checks(const QualificationChecks& checks, std::string message) {
    QualificationResult result{};
    result.status = status_from_checks(checks);
    result.build_exact_match = checks.fingerprint_matches_profile;
    result.module_valid = checks.module_ok && checks.pe_ok;
    result.anchor_valid = result.status == QualificationStatus::Qualified;
    if (message.empty()) {
        result.message = status_message(result.status);
    } else {
        result.message = std::move(message);
    }
    return result;
}

QualificationResult qualify_observation(const deflux::core::ProcessObservation& observation) {
    QualificationChecks checks{};

    if (observation.state == deflux::core::ProcessLifecycle::ObservationError) {
        checks.running = true;
        checks.identity_ok = false;
        return result_from_checks(checks, observation.message.empty()
                                              ? std::string(status_message(QualificationStatus::Error))
                                              : observation.message);
    }

    if (observation.state != deflux::core::ProcessLifecycle::ProcessDetected
        || !deflux::core::is_valid_process_instance(observation.instance)) {
        return result_from_checks(checks, "No verified GTA process is running.");
    }
    checks.running = true;
    checks.identity_ok = true;

    const EnvironmentPreflightResult preflight =
        scan_game_executable_directory(observation.instance.executable_path);
    if (!preflight.clean) {
        return result_from_environment_contamination(preflight);
    }

    const std::wstring path = detail::utf8_to_wide(observation.instance.executable_path);
    const auto fingerprint = detail::fingerprint_executable(path);
    checks.fingerprint_ok = fingerprint.ok && !fingerprint.sha256_hex.empty();
    if (!checks.fingerprint_ok) {
        return result_from_checks(checks, "Failed to re-hash the verified executable.");
    }

    const QualificationProfile* profile = find_qualification_profile(fingerprint.sha256_hex);
    checks.profile_found = profile != nullptr;
    if (!checks.profile_found) {
        return result_from_checks(checks,
                                  "This executable fingerprint has no runtime qualification profile.");
    }
    checks.fingerprint_matches_profile = fingerprint_matches_profile(
        *profile, fingerprint.sha256_hex, fingerprint.size_bytes, fingerprint.file_version);
    if (!checks.fingerprint_matches_profile) {
        return result_from_checks(checks,
                                  "On-disk fingerprint does not match the qualification profile.");
    }

    detail::ScopedProcess process;
    if (!process.open_read_only(observation.instance.process_id)) {
        checks.inspect_ok = false;
        return result_from_checks(checks, "OpenProcess(QUERY_LIMITED|VM_READ) failed.");
    }

    std::uint32_t live_pid = 0;
    std::uint64_t live_created = 0;
    std::wstring live_path;
    if (!process.query_identity(live_pid, live_created, live_path)) {
        checks.identity_stable = false;
        return result_from_checks(checks, "Could not re-query process identity.");
    }

    deflux::core::ProcessInstance live{};
    live.process_id = live_pid;
    live.creation_identity = live_created;
    live.executable_path = detail::wide_to_utf8(live_path);
    checks.identity_stable = deflux::core::same_process_instance(observation.instance, live)
        && executable_paths_match(observation.instance.executable_path, live.executable_path);
    if (!checks.identity_stable) {
        return result_from_checks(checks, "Process identity changed during qualification; aborting.");
    }

    const auto module = detail::query_main_module(live_pid, live_path);
    checks.module_ok = module.found && module.base != 0 && module.size != 0;
    if (!checks.module_ok) {
        return result_from_checks(checks,
                                  module.message.empty() ? std::string("Main module discovery failed.")
                                                         : module.message);
    }

    std::vector<std::uint8_t> headers(0x400, 0);
    if (!process.read_exact(module.base, headers.data(), headers.size())) {
        checks.pe_ok = false;
        return result_from_checks(checks, "Failed to read PE headers from the main module.");
    }

    PeImageInfo pe{};
    PeParseStatus pe_status = parse_pe64_headers(headers.data(), headers.size(), pe);
    if (pe_status == PeParseStatus::Ok && pe.size_of_headers > headers.size() && pe.size_of_headers <= 0x2000u) {
        headers.assign(pe.size_of_headers, 0);
        if (!process.read_exact(module.base, headers.data(), headers.size())) {
            checks.pe_ok = false;
            return result_from_checks(checks, "Failed to read full PE headers from the main module.");
        }
        pe_status = parse_pe64_headers(headers.data(), headers.size(), pe);
    }
    checks.pe_ok = pe_status == PeParseStatus::Ok && pe.size_of_image == static_cast<std::uint32_t>(module.size);
    if (!checks.pe_ok) {
        std::string msg = "PE validation failed: ";
        msg += to_string(pe_status);
        if (pe_status == PeParseStatus::Ok) {
            msg = "PE SizeOfImage does not match the live module size.";
        }
        return result_from_checks(checks, std::move(msg));
    }

    const std::uint32_t rva = profile->gworld_slot_rva;
    checks.rva_in_image = rva >= pe.size_of_headers && rva < pe.size_of_image
        && static_cast<std::uint64_t>(rva) + 8ull <= module.size;
    if (!checks.rva_in_image) {
        return result_from_checks(checks, "GWorld slot RVA is outside the validated image.");
    }
    checks.rva_in_data_section = pe_rva_in_non_executable_section(headers.data(), headers.size(), rva);
    if (!checks.rva_in_data_section) {
        return result_from_checks(checks, "GWorld slot RVA is not in a non-executable section.");
    }

    std::uint64_t slot_pointer = 0;
    const bool slot_read = process.read_exact(module.base + rva, &slot_pointer, sizeof(slot_pointer));
    std::uint64_t vtable = 0;
    bool object_read = false;
    if (slot_read && slot_pointer != 0) {
        object_read = process.read_exact(slot_pointer, &vtable, sizeof(vtable));
    }
    checks.anchor = evaluate_gworld_anchor(
        slot_read, slot_pointer, object_read, vtable, module.base, module.size);

    std::uint32_t again_pid = 0;
    std::uint64_t again_created = 0;
    std::wstring again_path;
    if (!process.query_identity(again_pid, again_created, again_path)) {
        checks.identity_stable = false;
        return result_from_checks(checks, "Process identity could not be confirmed after the probe.");
    }
    live.process_id = again_pid;
    live.creation_identity = again_created;
    live.executable_path = detail::wide_to_utf8(again_path);
    checks.identity_stable = deflux::core::same_process_instance(observation.instance, live);
    if (!checks.identity_stable) {
        return result_from_checks(checks, "Process identity changed during qualification; aborting.");
    }

    std::string msg = status_message(status_from_checks(checks));
    if (checks.anchor != AnchorCheck::Ok) {
        msg += " anchor_check=";
        msg += to_string(checks.anchor);
    }
    return result_from_checks(checks, std::move(msg));
}

} // namespace deflux::game::gta_sa_de
