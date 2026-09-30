#pragma once

#include "deflux/core/process_lifecycle.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace deflux::game::gta_sa_de {

// External read-only runtime qualification. Independent of Compatibility
// (fingerprint support), ProcessLifecycle, RuntimeHost lifecycle, engine/world/
// player readiness, and SessionState.
enum class QualificationStatus : std::uint8_t {
    NotRunning = 0,
    EnvironmentContaminated,
    BuildMismatch,
    ProcessChanged,
    ModuleInvalid,
    ProfileMissing,
    AnchorNotFound,
    AnchorInvalid,
    Qualified,
    Error,
};

enum class GWorldAnchorTrust : std::uint8_t {
    LegacyCandidateUnverified = 0,
};

enum class PeParseStatus : std::uint8_t {
    Ok = 0,
    InvalidArgument,
    TooSmall,
    BadDosMagic,
    BadNewHeaderOffset,
    BadNtSignature,
    NotAmd64,
    NotPe32Plus,
    ImplausibleImageSize,
};

enum class AnchorCheck : std::uint8_t {
    Unread = 0,
    SlotUnreadable,
    Null,
    NonCanonical,
    Unaligned,
    InsideModule,
    ObjectUnreadable,
    VtableNonCanonical,
    VtableOutsideModule,
    Ok,
};

struct PeImageInfo {
    std::uint32_t size_of_image = 0;
    std::uint32_t size_of_headers = 0;
    std::uint16_t machine = 0;
    std::uint16_t number_of_sections = 0;
    bool pe32_plus = false;
};

struct QualificationProfile {
    const char* sha256_hex = "";
    const char* file_version = "";
    std::uint64_t size_bytes = 0;
    std::uint32_t gworld_slot_rva = 0;
    const char* anchor_name = "";
    GWorldAnchorTrust anchor_trust = GWorldAnchorTrust::LegacyCandidateUnverified;
};

struct EnvironmentContaminant {
    std::string filename;
    std::string label;
    std::string path;
};

struct EnvironmentPreflightResult {
    bool clean = false;
    std::string scanned_directory;
    std::vector<EnvironmentContaminant> contaminants;
    std::string message;
};

// Fail-closed combination of gathered checks. QUALIFIED only if every required
// field is true and the anchor check is Ok.
struct QualificationChecks {
    bool running = false;
    bool identity_ok = false;
    bool identity_stable = true;
    bool fingerprint_ok = false;
    bool profile_found = false;
    bool fingerprint_matches_profile = false;
    bool inspect_ok = true;
    bool module_ok = false;
    bool pe_ok = false;
    bool rva_in_image = false;
    bool rva_in_data_section = false;
    AnchorCheck anchor = AnchorCheck::Unread;
};

struct QualificationResult {
    QualificationStatus status = QualificationStatus::NotRunning;
    bool build_exact_match = false;
    bool module_valid = false;
    bool anchor_valid = false;
    std::string message;
};

const char* to_string(QualificationStatus status) noexcept;
const char* to_string(PeParseStatus status) noexcept;
const char* to_string(AnchorCheck check) noexcept;
const char* to_string(GWorldAnchorTrust trust) noexcept;

EnvironmentPreflightResult scan_game_executable_directory(std::string_view executable_utf8_path);
QualificationResult result_from_environment_contamination(const EnvironmentPreflightResult& preflight);

bool is_canonical_user_pointer(std::uint64_t pointer) noexcept;
bool is_aligned_pointer(std::uint64_t pointer) noexcept;
bool pointer_in_range(std::uint64_t pointer, std::uint64_t base, std::uint64_t size) noexcept;
bool complete_read_ok(std::size_t requested, std::size_t copied, bool api_success) noexcept;

PeParseStatus parse_pe64_headers(const std::uint8_t* data, std::size_t size, PeImageInfo& out) noexcept;
bool pe_rva_in_non_executable_section(const std::uint8_t* data, std::size_t size, std::uint32_t rva) noexcept;

const QualificationProfile* find_qualification_profile(std::string_view sha256_hex) noexcept;
bool fingerprint_matches_profile(const QualificationProfile& profile,
                                 std::string_view sha256_hex,
                                 std::uint64_t size_bytes,
                                 std::string_view file_version) noexcept;

AnchorCheck evaluate_gworld_anchor(bool slot_read_ok,
                                   std::uint64_t slot_pointer,
                                   bool object_read_ok,
                                   std::uint64_t vtable_pointer,
                                   std::uint64_t module_base,
                                   std::uint64_t module_size) noexcept;

QualificationStatus status_from_checks(const QualificationChecks& checks) noexcept;
QualificationResult result_from_checks(const QualificationChecks& checks, std::string message);

enum class GWorldDiscoveryStatus : std::uint8_t {
    Ok = 0,
    InvalidArgument,
    NoExecutableSections,
    ZeroMatches,
    AmbiguousMatches,
};

struct GWorldSignatureMatch {
    std::uint32_t instruction_rva = 0;
    std::uint32_t slot_rva = 0;
    std::uint8_t pattern_id = 0;
};

struct GWorldDiscoveryResult {
    GWorldDiscoveryStatus status = GWorldDiscoveryStatus::InvalidArgument;
    std::vector<GWorldSignatureMatch> matches;
    std::uint32_t unique_slot_rva = 0;
    std::size_t unique_slot_count = 0;
    std::string message;
};

struct QualificationInvestigation {
    QualificationResult qualification{};
    std::uint64_t module_base = 0;
    std::uint32_t size_of_image = 0;
    char profile_section[9]{};
    bool profile_slot_read_ok = false;
    std::uint64_t profile_slot_value = 0;
    GWorldDiscoveryResult offline_discovery{};
    GWorldDiscoveryResult live_discovery{};
    std::uint32_t signature_unique_slot_rva = 0;
    std::size_t signature_unique_slot_count = 0;
    bool profile_rva_matches_signature = false;
    std::size_t profile_rva_signature_hits = 0;
    std::string summary;
};

// Live external probe. Uses the Task #4 verified process identity. Does not
// inject, hook, or write. Does not load DeFlux_Runtime.dll into GTA.
QualificationResult qualify_observation(const deflux::core::ProcessObservation& observation);
QualificationInvestigation investigate_qualification(
    const deflux::core::ProcessObservation& observation) noexcept;

GWorldDiscoveryResult discover_gworld_slots_in_image(const std::uint8_t* image,
                                                     std::size_t image_size,
                                                     std::uint32_t size_of_image) noexcept;

std::uint32_t decode_x64_rip_relative(std::uint32_t instruction_rva,
                                      const std::uint8_t* instruction_bytes,
                                      std::uint32_t displacement_offset,
                                      std::uint32_t instruction_length) noexcept;

bool match_signature(const std::uint8_t* data,
                     std::size_t size,
                     const std::uint8_t* pattern,
                     const char* mask) noexcept;

} // namespace deflux::game::gta_sa_de
