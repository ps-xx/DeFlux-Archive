#include "deflux/game/gta_sa_de/adapter.hpp"

#include "detection_internal.hpp"
#include "gworld_discovery.hpp"
#include "observation.hpp"

#include <string>

namespace deflux::game::gta_sa_de {
namespace {

constexpr const char* kReadinessMessage =
    "Not implemented: process observation is not engine, world, or local-player readiness.";

} // namespace

AdapterIdentity GtaSaDeAdapter::identity() const {
    return AdapterIdentity{
        "gta_sa_de",
        "Grand Theft Auto: San Andreas - The Definitive Edition",
    };
}

version::SemanticVersion GtaSaDeAdapter::api_version() const {
    return version::kGameAdapterApi;
}

DetectionSnapshot GtaSaDeAdapter::detect() const {
    return detect_installation();
}

DetectionSnapshot GtaSaDeAdapter::detect_from_executable(std::string_view utf8_path) const {
    return detect_explicit_executable(utf8_path);
}

deflux::core::ProcessObservation GtaSaDeAdapter::observe() {
    last_observation_ = deflux::core::advance_process_lifecycle(last_observation_, sample_live_process());
    return last_observation_;
}

const deflux::core::ProcessObservation& GtaSaDeAdapter::last_observation() const noexcept {
    return last_observation_;
}

QualificationResult GtaSaDeAdapter::qualify() {
    const auto observation = observe();
    return qualify_observation(observation);
}

QualificationInvestigation GtaSaDeAdapter::investigate() {
    const auto observation = observe();
    return investigate_qualification(observation);
}

ProbeResult GtaSaDeAdapter::map_probe(const DetectionSnapshot& snap, std::string& storage) const {
    storage = snap.message;
    return ProbeResult{to_probe_status(snap.compatibility), storage.c_str()};
}

ProbeResult GtaSaDeAdapter::map_build(const DetectionSnapshot& snap, std::string& storage) const {
    storage.clear();
    if (!snap.sha256_hex.empty()) {
        storage = "sha256=";
        storage += snap.sha256_hex;
        if (snap.file_size_bytes > 0) {
            storage += " size=";
            storage += std::to_string(snap.file_size_bytes);
        }
        if (!snap.file_version.empty()) {
            storage += " file_version=";
            storage += snap.file_version;
        }
        storage += " ";
        storage += snap.message;
    } else {
        storage = snap.message;
    }
    return ProbeResult{to_probe_status(snap.compatibility), storage.c_str()};
}

ProbeResult GtaSaDeAdapter::probe_supported_game() const {
    return map_probe(detect(), probe_message_);
}

ProbeResult GtaSaDeAdapter::detected_game_build() const {
    return map_build(detect(), build_message_);
}

ReadinessResult GtaSaDeAdapter::engine_ready() const {
    return ReadinessResult{Readiness::NotImplemented, kReadinessMessage};
}

ReadinessResult GtaSaDeAdapter::world_ready() const {
    return ReadinessResult{Readiness::NotImplemented, kReadinessMessage};
}

ReadinessResult GtaSaDeAdapter::local_player_ready() const {
    return ReadinessResult{Readiness::NotImplemented, kReadinessMessage};
}

} // namespace deflux::game::gta_sa_de
