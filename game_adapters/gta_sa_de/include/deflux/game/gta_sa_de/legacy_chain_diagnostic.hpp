#pragma once

// Task #8D: research-only external read-only legacy local-player chain diagnostic.
// Not GWorld qualification. Not local-player qualification. Not readiness.

#include "deflux/core/process_lifecycle.hpp"
#include "deflux/game/gta_sa_de/gworld_live_observation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace deflux::game::gta_sa_de {

inline constexpr std::uint32_t kLegacyChainDefaultIntervalMs = 500;
inline constexpr std::uint32_t kLegacyChainDefaultMaxSamples = 120;
inline constexpr std::uint32_t kLegacyChainMaxIntervalMs = 5000;
inline constexpr std::uint32_t kLegacyChainMaxSamples = 120;
inline constexpr std::size_t kLegacyChainPointerHopBytes = 8;
inline constexpr std::size_t kLegacyChainMaxPointerDepth = 7;
inline constexpr std::size_t kLegacyChainTransformReadBytes = 0x28;
inline constexpr float kLegacyChainMovementEpsilon = 1.0f;

inline constexpr std::array<std::uint32_t, 2> kLegacyChainOpaqueRootRvas = kArchitectApprovedCandidateRvas;

struct LegacyOffsetProfile {
    const char* name;
    std::uint32_t owning_game_instance;
    std::uint32_t local_players;
    std::uint32_t player_controller;
    std::uint32_t pawn;
    std::uint32_t root_component;
    std::uint32_t component_to_world;
};

inline constexpr std::array<LegacyOffsetProfile, 9> kLegacyChainProfiles = {{
    {"ue4.26-a", 0x180, 0x38, 0x30, 0x2A0, 0x130, 0x1C0},
    {"ue4.26-b", 0x188, 0x38, 0x30, 0x2A8, 0x138, 0x1C0},
    {"ue4.26-c", 0x1A0, 0x40, 0x38, 0x2B0, 0x190, 0x1C0},
    {"sade-ish", 0x1F8, 0x40, 0x38, 0x2A0, 0x130, 0x1C0},
    {"sade-b", 0x1F8, 0x40, 0x38, 0x430, 0x220, 0x230},
    {"ue4.27", 0x1C0, 0x40, 0x30, 0x2A0, 0x168, 0x1C0},
    {"wide-a", 0x1F8, 0x38, 0x30, 0x2A0, 0x130, 0x1C0},
    {"wide-b", 0x180, 0x40, 0x38, 0x2A0, 0x138, 0x1C0},
    {"ackpawn", 0x180, 0x38, 0x30, 0x2A8, 0x130, 0x1C0},
}};

enum class LegacyChainBreakStage : std::uint8_t {
    Ok = 0,
    RootSlotReadFailed,
    RootNull,
    ProfileRootReadFailed,
    ProfileRootNull,
    LocalPlayersReadFailed,
    LocalPlayersBad,
    LocalPlayerReadFailed,
    LocalPlayerNull,
    LegacyPcCandidateReadFailed,
    LegacyPcCandidateNull,
    LegacyPawnCandidateReadFailed,
    LegacyPawnCandidateNull,
    RootComponentReadFailed,
    RootComponentNull,
    TransformReadFailed,
    TransformInvalid,
};

enum class LegacyChainClassification : std::uint8_t {
    Observed = 0,
    Insufficient,
    Contradicted,
};

struct LegacyChainVec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

struct LegacyChainTransformShape {
    LegacyChainVec3 translation{};
    float quat[4]{0.f, 0.f, 0.f, 1.f};
    float yaw_degrees = 0.f;
    bool shape_valid = false;
};

struct LegacyChainMovementDelta {
    float delta_x = 0.f;
    float delta_y = 0.f;
    float delta_z = 0.f;
    float distance = 0.f;
    bool changed_beyond_epsilon = false;
    bool has_previous = false;
};

struct LegacyChainProfileSample {
    std::uint32_t opaque_root_rva = 0;
    std::uint32_t profile_index = 0;
    const char* profile_name = "";
    LegacyChainBreakStage stage = LegacyChainBreakStage::RootSlotReadFailed;
    std::uint64_t opaque_root_slot_va = 0;
    std::uint64_t profile_root_va = 0;
    std::uint64_t legacy_pc_candidate_va = 0;
    std::uint64_t legacy_pawn_candidate_va = 0;
    std::uint64_t root_component_va = 0;
    LegacyChainTransformShape transform{};
    LegacyChainMovementDelta movement{};
    bool local_player_candidate_preserved = false;
};

struct LegacyChainObservationSample {
    std::uint32_t sample_index = 0;
    std::uint64_t timestamp_ms = 0;
    LiveObservationPhase phase = LiveObservationPhase::Unknown;
    bool process_identity_valid = true;
    bool game_state_sampled = false;
    bool game_state_read_ok = false;
    std::uint8_t raw_game_state = 0;
    std::vector<LegacyChainProfileSample> profiles;
};

struct LegacyChainDiagnosticConfig {
    const char* expected_sha256 = kLiveObservationTargetSha256;
    std::uint32_t interval_ms = kLegacyChainDefaultIntervalMs;
    std::uint32_t max_samples = kLegacyChainDefaultMaxSamples;
    bool sample_game_state_context = true;
};

struct LegacyChainDiagnosticSessionResult {
    LiveObservationStatus status = LiveObservationStatus::NotRunning;
    std::string message;
    LegacyChainClassification classification = LegacyChainClassification::Insufficient;
    std::string classification_rationale;
    std::uint32_t process_id = 0;
    std::uint64_t module_base = 0;
    std::uint64_t module_size = 0;
    std::size_t samples_collected = 0;
    std::vector<LegacyChainObservationSample> samples;
};

using LegacyChainReadFn = std::function<bool(std::uint64_t address, void* out, std::size_t bytes)>;

const char* to_string(LegacyChainBreakStage stage) noexcept;
const char* to_string(LegacyChainClassification classification) noexcept;

bool is_legacy_chain_opaque_root_rva(std::uint32_t candidate_rva) noexcept;

bool normalize_legacy_chain_diagnostic_config(LegacyChainDiagnosticConfig& config) noexcept;

bool validate_legacy_transform_shape(const float quat[4],
                                     const LegacyChainVec3& translation,
                                     const LegacyChainVec3& scale,
                                     LegacyChainTransformShape& out) noexcept;

LegacyChainMovementDelta compute_legacy_chain_movement_delta(
    const LegacyChainTransformShape& previous,
    const LegacyChainTransformShape& current,
    float epsilon) noexcept;

LegacyChainProfileSample probe_legacy_chain_profile(std::uint64_t opaque_root_slot_va,
                                                    const LegacyOffsetProfile& profile,
                                                    LegacyChainReadFn read) noexcept;

LegacyChainClassification classify_legacy_chain_session(
    const std::vector<LegacyChainObservationSample>& samples) noexcept;

LegacyChainDiagnosticSessionResult run_legacy_chain_diagnostic_session(
    const deflux::core::ProcessObservation& observation,
    LegacyChainDiagnosticConfig config,
    const std::function<bool(LiveObservationPhase& phase_out)>& next_phase);

} // namespace deflux::game::gta_sa_de
