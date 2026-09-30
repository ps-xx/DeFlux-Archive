#pragma once

// Task #8B: research-only external read-only gGameState observation.
// Not readiness. Not invoked by normal runtime qualification.

#include "deflux/core/process_lifecycle.hpp"
#include "deflux/game/gta_sa_de/gworld_live_observation.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace deflux::game::gta_sa_de {

// Legacy constants from the omitted client (client/src/game/sa_interface_boot.cpp).
// That client is intentionally excluded from this snapshot. Meanings are assumed.
inline constexpr std::uint8_t kLegacyGsFrontend = 7;
inline constexpr std::uint8_t kLegacyGsInitPlayingGame = 8;
inline constexpr std::uint8_t kLegacyGsPlayingGame = 9;
inline constexpr std::uint8_t kLegacyGsCmpImmediate = 9;
inline constexpr char kLegacyGameStateAnchorString[] = "menu_mainpresents";

inline constexpr std::uint32_t kGameStateObservationDefaultIntervalMs = 500;
inline constexpr std::uint32_t kGameStateObservationDefaultMaxSamples = 120;
inline constexpr std::uint32_t kGameStateObservationMaxIntervalMs = 5000;
inline constexpr std::uint32_t kGameStateObservationMaxSamples = 120;

enum class GameStateAddressDerivation : std::uint8_t {
    None = 0,
    MenuMainPresentsLeaCmpImm9,
};

enum class GameStateObservationClassification : std::uint8_t {
    Observed = 0,
    Insufficient,
    Contradicted,
};

struct GameStateAddressResolution {
    bool resolved = false;
    GameStateAddressDerivation method = GameStateAddressDerivation::None;
    std::uint32_t global_rva = 0;
    std::uint32_t cmp_instruction_rva = 0;
    std::uint32_t anchor_string_rva = 0;
    std::size_t lea_xref_count = 0;
    std::string message;
};

struct GameStateObservationSample {
    std::uint32_t sample_index = 0;
    std::uint64_t timestamp_ms = 0;
    LiveObservationPhase phase = LiveObservationPhase::Unknown;
    GameStateAddressDerivation derivation_method = GameStateAddressDerivation::None;
    std::uint32_t global_rva = 0;
    std::uint64_t global_va = 0;
    bool read_ok = false;
    std::uint8_t raw_value = 0;
    bool process_identity_valid = true;
};

struct GameStatePhaseSummary {
    LiveObservationPhase phase = LiveObservationPhase::Unknown;
    std::size_t samples = 0;
    std::size_t read_successes = 0;
    std::size_t distinct_values = 0;
};

struct GameStateObservationConfig {
    const char* expected_sha256 = kLiveObservationTargetSha256;
    std::uint32_t interval_ms = kGameStateObservationDefaultIntervalMs;
    std::uint32_t max_samples = kGameStateObservationDefaultMaxSamples;
};

struct GameStateObservationSessionResult {
    LiveObservationStatus status = LiveObservationStatus::NotRunning;
    std::string message;
    GameStateAddressResolution resolution{};
    GameStateObservationClassification classification = GameStateObservationClassification::Insufficient;
    std::string classification_rationale;
    std::uint32_t process_id = 0;
    std::uint64_t module_base = 0;
    std::uint64_t module_size = 0;
    std::size_t samples_collected = 0;
    std::vector<GameStateObservationSample> samples;
    std::vector<GameStatePhaseSummary> by_phase;
};

const char* to_string(GameStateAddressDerivation derivation) noexcept;
const char* to_string(GameStateObservationClassification classification) noexcept;

bool normalize_game_state_observation_config(GameStateObservationConfig& config) noexcept;

bool resolve_ggame_state_from_module_image(const std::uint8_t* image,
                                           std::size_t image_size,
                                           GameStateAddressResolution& out) noexcept;

GameStateObservationClassification classify_game_state_observation(
    bool address_resolved,
    std::size_t successful_reads,
    std::size_t read_attempts) noexcept;

std::vector<GameStatePhaseSummary> summarize_game_state_observation_by_phase(
    const std::vector<GameStateObservationSample>& samples) noexcept;

GameStateObservationSessionResult run_game_state_observation_session(
    const deflux::core::ProcessObservation& observation,
    GameStateObservationConfig config,
    const std::function<bool(LiveObservationPhase& phase_out)>& next_phase);

} // namespace deflux::game::gta_sa_de
