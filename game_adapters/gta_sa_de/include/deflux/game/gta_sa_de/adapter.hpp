#pragma once

#include "deflux/core/process_lifecycle.hpp"
#include "deflux/game/adapter.hpp"
#include "deflux/game/gta_sa_de/detection.hpp"
#include "deflux/game/gta_sa_de/qualification.hpp"

#include <string_view>

namespace deflux::game::gta_sa_de {

// GTA SA DE adapter: identity, passive install/process detection, fingerprinting,
// process-lifecycle observation, and an external read-only runtime qualification
// probe. Does not inject, hook, write game memory, or report engine/world/player
// readiness from a discovered GWorld slot.
class GtaSaDeAdapter final : public IGameAdapter {
public:
    AdapterIdentity identity() const override;
    version::SemanticVersion api_version() const override;
    ProbeResult probe_supported_game() const override;
    ProbeResult detected_game_build() const override;
    ReadinessResult engine_ready() const override;
    ReadinessResult world_ready() const override;
    ReadinessResult local_player_ready() const override;

    DetectionSnapshot detect() const;
    DetectionSnapshot detect_from_executable(std::string_view utf8_path) const;

    // Explicit poll. Does not start a background thread. PROCESS_DETECTED does
    // not imply compatibility, attachment, or readiness.
    deflux::core::ProcessObservation observe();
    const deflux::core::ProcessObservation& last_observation() const noexcept;

    // External read-only runtime qualification of the Task #4 process instance.
    // Does not inject, hook, write, or load DeFlux_Runtime.dll into GTA.
    QualificationResult qualify();

    // Developer-only investigation (signature discovery, slot diagnostics).
    QualificationInvestigation investigate();

private:
    ProbeResult map_probe(const DetectionSnapshot& snap, std::string& storage) const;
    ProbeResult map_build(const DetectionSnapshot& snap, std::string& storage) const;

    mutable std::string probe_message_;
    mutable std::string build_message_;
    deflux::core::ProcessObservation last_observation_{};
};

} // namespace deflux::game::gta_sa_de
