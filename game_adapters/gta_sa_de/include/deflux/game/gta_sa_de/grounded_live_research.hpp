#pragma once

// Task #14C: explicit read-only live entry. Research only.
// This does not weaken DEFLUX_RESEARCH_ISOLATE_LIVE_TARGET.
// evaluate_grounded_live_attach() remains blocked.
// A strong result is a transform candidate, not local-player identity.

#include <cstdint>
#include <string>
#include <string_view>

namespace deflux::game::gta_sa_de {

inline constexpr const char kGroundedLiveFlag[] = "--grounded-live";
inline constexpr const char kGroundedCleoAbsentFlag[] = "--cleo-absent";
inline constexpr std::uint64_t kGroundedExpectedFileSize = 89497064ull;
inline constexpr const char kGroundedExpectedVersion[] = "1.0.113.21181";

// Ultimate ASI Loader v9.7.4 x64 (official asset dinput8.dll).
// Installed as version.dll because CLEO documents that name for 64-bit games
// and SanAndreas.exe imports VERSION.dll. This is not a DeFlux loader.
inline constexpr const char kOfficialAsiLoaderSha256[] =
    "fa266e3513d02c08a1b808f28c10538a489eaffaa4b0707f7cc1066e71b5afd7";

enum class GroundedLiveBlock : std::uint32_t {
    Ready = 0,
    IsolationEngaged,
    IntentMissing,
    TargetAbsent,
    BuildSizeMismatch,
    BuildShaMismatch,
    BuildVersionMismatch,
    ProcessPathMismatch,
    CreationMissing,
    ModuleIdentityMismatch,
    WriteAccessRejected,
};

struct GroundedLiveGateInput {
    bool isolation_engaged = false;
    bool explicit_intent = false;
    bool process_present = false;
    bool path_matches = false;
    bool size_matches = false;
    bool sha_matches = false;
    bool version_matches = false;
    bool creation_present = false;
    bool module_identity_matches = false;
    std::uint32_t requested_access = 0;
};

GroundedLiveBlock evaluate_grounded_live_gate(const GroundedLiveGateInput& input) noexcept;
const char* grounded_live_block_message(GroundedLiveBlock block) noexcept;

std::uint32_t grounded_live_process_access() noexcept;
bool grounded_live_access_is_read_only(std::uint32_t access) noexcept;
bool grounded_asi_loader_sha_is_official(std::string_view sha256_hex) noexcept;

// First-process runs never return the success token.
// The success token is returned only when the separate-process control passed.
const char* grounded_live_result_token(bool first_process,
                                       std::size_t strong_count,
                                       bool cleo_absent_control_passed) noexcept;

// Interactive read-only session. Returns 0 when the session finished,
// 2 when attachment was refused, 3 when the experiment was invalid.
int run_grounded_live_console(bool cleo_absent, std::string& message_out);

} // namespace deflux::game::gta_sa_de
