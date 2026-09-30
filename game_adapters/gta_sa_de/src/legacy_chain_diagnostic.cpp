#include "deflux/game/gta_sa_de/legacy_chain_diagnostic.hpp"

#include "deflux/game/gta_sa_de/ggame_state_observation.hpp"
#include "deflux/game/gta_sa_de/qualification.hpp"
#include "environment_preflight.hpp"
#include "fingerprint.hpp"
#include "module_query.hpp"
#include "observation.hpp"
#include "remote_read.hpp"
#include "win_utf8.hpp"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <unordered_map>

namespace deflux::game::gta_sa_de {
namespace {

bool read_pointer(LegacyChainReadFn& read, std::uint64_t address, std::uint64_t& out) {
    if (!read(address, &out, kLegacyChainPointerHopBytes)) {
        return false;
    }
    return true;
}

bool read_i32(LegacyChainReadFn& read, std::uint64_t address, std::int32_t& out) {
    if (!read(address, &out, sizeof(out))) {
        return false;
    }
    return true;
}

float yaw_from_quat(float x, float y, float z, float w) {
    const float siny_cosp = 2.f * (w * z + x * y);
    const float cosy_cosp = 1.f - 2.f * (y * y + z * z);
    return std::atan2(siny_cosp, cosy_cosp) * (180.f / 3.14159265f);
}

bool plausible_world_pos(const LegacyChainVec3& p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
        return false;
    }
    if (p.x < -4000.f || p.x > 4000.f) {
        return false;
    }
    if (p.y < -4000.f || p.y > 4000.f) {
        return false;
    }
    if (p.z < -50.f || p.z > 1500.f) {
        return false;
    }
    if (std::fabs(p.x) < 1.f && std::fabs(p.y) < 1.f && std::fabs(p.z) < 1.f) {
        return false;
    }
    const float mag2 = p.x * p.x + p.y * p.y + p.z * p.z;
    if (mag2 < 4.f) {
        return false;
    }
    return true;
}

LegacyChainDiagnosticSessionResult make_status(LiveObservationStatus status, std::string message) {
    LegacyChainDiagnosticSessionResult result{};
    result.status = status;
    result.message = std::move(message);
    return result;
}

std::uint64_t steady_timestamp_ms() {
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
}

void sleep_interval_ms(std::uint32_t interval_ms) {
    if (interval_ms == 0) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
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
        message_out = "Process identity changed during legacy chain diagnostic; aborting.";
        return false;
    }
    return true;
}

bool read_module_image(detail::ScopedProcess& process,
                       std::uint64_t module_base,
                       std::uint64_t module_size,
                       std::vector<std::uint8_t>& out) {
    if (module_base == 0 || module_size == 0 || module_size > 0x40000000ull) {
        return false;
    }
    out.assign(static_cast<std::size_t>(module_size), 0);
    constexpr std::size_t kChunk = 1024 * 1024;
    std::size_t offset = 0;
    while (offset < out.size()) {
        const std::size_t chunk = std::min(kChunk, out.size() - offset);
        if (!process.read_exact(module_base + offset, out.data() + offset, chunk)) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

struct ProfileHistoryKey {
    std::uint32_t root_rva = 0;
    std::uint32_t profile_index = 0;

    bool operator==(const ProfileHistoryKey& other) const noexcept {
        return root_rva == other.root_rva && profile_index == other.profile_index;
    }
};

struct ProfileHistoryKeyHash {
    std::size_t operator()(const ProfileHistoryKey& key) const noexcept {
        return static_cast<std::size_t>(key.root_rva) ^ (static_cast<std::size_t>(key.profile_index) << 16);
    }
};

} // namespace

const char* to_string(LegacyChainBreakStage stage) noexcept {
    switch (stage) {
    case LegacyChainBreakStage::Ok:
        return "OK";
    case LegacyChainBreakStage::RootSlotReadFailed:
        return "ROOT_SLOT_READ_FAILED";
    case LegacyChainBreakStage::RootNull:
        return "ROOT_NULL";
    case LegacyChainBreakStage::ProfileRootReadFailed:
        return "PROFILE_ROOT_READ_FAILED";
    case LegacyChainBreakStage::ProfileRootNull:
        return "PROFILE_ROOT_NULL";
    case LegacyChainBreakStage::LocalPlayersReadFailed:
        return "LOCAL_PLAYERS_READ_FAILED";
    case LegacyChainBreakStage::LocalPlayersBad:
        return "LOCAL_PLAYERS_BAD";
    case LegacyChainBreakStage::LocalPlayerReadFailed:
        return "LOCAL_PLAYER_READ_FAILED";
    case LegacyChainBreakStage::LocalPlayerNull:
        return "LOCAL_PLAYER_NULL";
    case LegacyChainBreakStage::LegacyPcCandidateReadFailed:
        return "LEGACY_PC_CANDIDATE_READ_FAILED";
    case LegacyChainBreakStage::LegacyPcCandidateNull:
        return "LEGACY_PC_CANDIDATE_NULL";
    case LegacyChainBreakStage::LegacyPawnCandidateReadFailed:
        return "LEGACY_PAWN_CANDIDATE_READ_FAILED";
    case LegacyChainBreakStage::LegacyPawnCandidateNull:
        return "LEGACY_PAWN_CANDIDATE_NULL";
    case LegacyChainBreakStage::RootComponentReadFailed:
        return "ROOT_COMPONENT_READ_FAILED";
    case LegacyChainBreakStage::RootComponentNull:
        return "ROOT_COMPONENT_NULL";
    case LegacyChainBreakStage::TransformReadFailed:
        return "TRANSFORM_READ_FAILED";
    case LegacyChainBreakStage::TransformInvalid:
        return "TRANSFORM_INVALID";
    }
    return "UNKNOWN";
}

const char* to_string(LegacyChainClassification classification) noexcept {
    switch (classification) {
    case LegacyChainClassification::Observed:
        return "LEGACY_CHAIN_OBSERVED";
    case LegacyChainClassification::Insufficient:
        return "LEGACY_CHAIN_INSUFFICIENT";
    case LegacyChainClassification::Contradicted:
        return "LEGACY_CHAIN_CONTRADICTED";
    }
    return "UNKNOWN";
}

bool is_legacy_chain_opaque_root_rva(std::uint32_t candidate_rva) noexcept {
    return is_architect_approved_candidate_rva(candidate_rva);
}

bool normalize_legacy_chain_diagnostic_config(LegacyChainDiagnosticConfig& config) noexcept {
    if (config.expected_sha256 == nullptr || config.expected_sha256[0] == '\0') {
        config.expected_sha256 = kLiveObservationTargetSha256;
    }
    if (config.interval_ms == 0) {
        config.interval_ms = kLegacyChainDefaultIntervalMs;
    }
    if (config.max_samples == 0) {
        config.max_samples = kLegacyChainDefaultMaxSamples;
    }
    if (config.interval_ms > kLegacyChainMaxIntervalMs) {
        config.interval_ms = kLegacyChainMaxIntervalMs;
    }
    if (config.max_samples > kLegacyChainMaxSamples) {
        config.max_samples = kLegacyChainMaxSamples;
    }
    return sha256_matches_live_observation_target(config.expected_sha256);
}

bool validate_legacy_transform_shape(const float quat[4],
                                     const LegacyChainVec3& translation,
                                     const LegacyChainVec3& scale,
                                     LegacyChainTransformShape& out) noexcept {
    out = LegacyChainTransformShape{};
    out.translation = translation;
    out.quat[0] = quat[0];
    out.quat[1] = quat[1];
    out.quat[2] = quat[2];
    out.quat[3] = quat[3];

    if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z)) {
        return false;
    }
    if (std::fabs(scale.x - 1.f) > 0.35f || std::fabs(scale.y - 1.f) > 0.35f
        || std::fabs(scale.z - 1.f) > 0.35f) {
        return false;
    }
    const float qn = quat[0] * quat[0] + quat[1] * quat[1] + quat[2] * quat[2] + quat[3] * quat[3];
    if (qn < 0.5f || qn > 1.5f) {
        return false;
    }
    if (!plausible_world_pos(translation)) {
        return false;
    }

    out.yaw_degrees = yaw_from_quat(quat[0], quat[1], quat[2], quat[3]);
    out.shape_valid = true;
    return true;
}

LegacyChainMovementDelta compute_legacy_chain_movement_delta(
    const LegacyChainTransformShape& previous,
    const LegacyChainTransformShape& current,
    float epsilon) noexcept {
    LegacyChainMovementDelta delta{};
    if (!previous.shape_valid || !current.shape_valid) {
        return delta;
    }
    delta.has_previous = true;
    delta.delta_x = current.translation.x - previous.translation.x;
    delta.delta_y = current.translation.y - previous.translation.y;
    delta.delta_z = current.translation.z - previous.translation.z;
    delta.distance = std::sqrt(delta.delta_x * delta.delta_x + delta.delta_y * delta.delta_y
                               + delta.delta_z * delta.delta_z);
    delta.changed_beyond_epsilon = delta.distance > epsilon;
    return delta;
}

LegacyChainProfileSample probe_legacy_chain_profile(std::uint64_t opaque_root_slot_va,
                                                    const LegacyOffsetProfile& profile,
                                                    LegacyChainReadFn read) noexcept {
    LegacyChainProfileSample result{};
    result.opaque_root_rva = 0;
    result.profile_name = profile.name;
    result.opaque_root_slot_va = opaque_root_slot_va;

    if (!read) {
        result.stage = LegacyChainBreakStage::RootSlotReadFailed;
        return result;
    }

    std::uint64_t profile_root = 0;
    if (!read_pointer(read, opaque_root_slot_va, profile_root)) {
        result.stage = LegacyChainBreakStage::RootSlotReadFailed;
        return result;
    }
    if (profile_root == 0 || !is_canonical_user_pointer(profile_root)) {
        result.stage = LegacyChainBreakStage::RootNull;
        return result;
    }
    result.profile_root_va = profile_root;

    std::uint64_t game_instance = 0;
    if (!read_pointer(read, profile_root + profile.owning_game_instance, game_instance)) {
        result.stage = LegacyChainBreakStage::ProfileRootReadFailed;
        return result;
    }
    if (game_instance == 0 || !is_canonical_user_pointer(game_instance)) {
        result.stage = LegacyChainBreakStage::ProfileRootNull;
        return result;
    }

    std::uint64_t local_players_data = 0;
    if (!read_pointer(read, game_instance + profile.local_players, local_players_data)) {
        result.stage = LegacyChainBreakStage::LocalPlayersReadFailed;
        return result;
    }
    std::int32_t local_players_count = 0;
    if (!read_i32(read, game_instance + profile.local_players + 8, local_players_count)
        || local_players_count <= 0 || local_players_count > 8 || local_players_data == 0
        || !is_canonical_user_pointer(local_players_data)) {
        result.stage = LegacyChainBreakStage::LocalPlayersBad;
        return result;
    }

    std::uint64_t local_player = 0;
    if (!read_pointer(read, local_players_data, local_player)) {
        result.stage = LegacyChainBreakStage::LocalPlayerReadFailed;
        return result;
    }
    if (local_player == 0 || !is_canonical_user_pointer(local_player)) {
        result.stage = LegacyChainBreakStage::LocalPlayerNull;
        return result;
    }

    std::uint64_t legacy_pc = 0;
    if (!read_pointer(read, local_player + profile.player_controller, legacy_pc)) {
        result.stage = LegacyChainBreakStage::LegacyPcCandidateReadFailed;
        return result;
    }
    if (legacy_pc == 0 || !is_canonical_user_pointer(legacy_pc)) {
        result.stage = LegacyChainBreakStage::LegacyPcCandidateNull;
        return result;
    }
    result.legacy_pc_candidate_va = legacy_pc;

    std::uint64_t legacy_pawn = 0;
    if (!read_pointer(read, legacy_pc + profile.pawn, legacy_pawn)) {
        result.stage = LegacyChainBreakStage::LegacyPawnCandidateReadFailed;
        return result;
    }
    if (legacy_pawn == 0 || !is_canonical_user_pointer(legacy_pawn)) {
        result.stage = LegacyChainBreakStage::LegacyPawnCandidateNull;
        return result;
    }
    result.legacy_pawn_candidate_va = legacy_pawn;

    std::uint64_t root_component = 0;
    if (!read_pointer(read, legacy_pawn + profile.root_component, root_component)) {
        result.stage = LegacyChainBreakStage::RootComponentReadFailed;
        return result;
    }
    if (root_component == 0 || !is_canonical_user_pointer(root_component)) {
        result.stage = LegacyChainBreakStage::RootComponentNull;
        return result;
    }
    result.root_component_va = root_component;

    std::uint8_t transform_bytes[kLegacyChainTransformReadBytes]{};
    if (!read(root_component + profile.component_to_world,
              transform_bytes,
              kLegacyChainTransformReadBytes)) {
        result.stage = LegacyChainBreakStage::TransformReadFailed;
        return result;
    }

    float quat[4]{};
    LegacyChainVec3 translation{};
    LegacyChainVec3 scale{};
    std::memcpy(quat, transform_bytes, sizeof(quat));
    std::memcpy(&translation.x, transform_bytes + 0x10, sizeof(float) * 3);
    std::memcpy(&scale.x, transform_bytes + 0x1C, sizeof(float) * 3);

    if (!validate_legacy_transform_shape(quat, translation, scale, result.transform)) {
        result.stage = LegacyChainBreakStage::TransformInvalid;
        return result;
    }

    result.stage = LegacyChainBreakStage::Ok;
    result.local_player_candidate_preserved = true;
    return result;
}

LegacyChainClassification classify_legacy_chain_session(
    const std::vector<LegacyChainObservationSample>& samples) noexcept {
    bool observed = false;
    bool contradicted = false;

    using HistoryMap = std::unordered_map<ProfileHistoryKey, LegacyChainTransformShape, ProfileHistoryKeyHash>;
    HistoryMap history;

    for (const LegacyChainObservationSample& sample : samples) {
        for (const LegacyChainProfileSample& profile : sample.profiles) {
            if (profile.transform.shape_valid) {
                observed = true;
            }

            ProfileHistoryKey key{profile.opaque_root_rva, profile.profile_index};
            const auto it = history.find(key);
            if (it != history.end() && it->second.shape_valid && profile.transform.shape_valid) {
                if (profile.stage != LegacyChainBreakStage::Ok) {
                    contradicted = true;
                }
            } else if (it != history.end() && it->second.shape_valid && !profile.transform.shape_valid
                       && profile.stage != LegacyChainBreakStage::Ok) {
                contradicted = true;
            }
            if (profile.transform.shape_valid) {
                history[key] = profile.transform;
            }
        }
    }

    if (contradicted) {
        return LegacyChainClassification::Contradicted;
    }
    if (observed) {
        return LegacyChainClassification::Observed;
    }
    return LegacyChainClassification::Insufficient;
}

LegacyChainDiagnosticSessionResult run_legacy_chain_diagnostic_session(
    const deflux::core::ProcessObservation& observation,
    LegacyChainDiagnosticConfig config,
    const std::function<bool(LiveObservationPhase& phase_out)>& next_phase) {
    if (!normalize_legacy_chain_diagnostic_config(config)) {
        return make_status(LiveObservationStatus::BuildMismatch,
                           "Legacy chain diagnostic requires the architect-approved target SHA-256.");
    }
    if (!next_phase) {
        return make_status(LiveObservationStatus::Error, "Phase provider callback is required.");
    }

    if (observation.state == deflux::core::ProcessLifecycle::ObservationError) {
        return make_status(LiveObservationStatus::Error,
                           observation.message.empty()
                               ? std::string("Process observation error.")
                               : observation.message);
    }
    if (observation.state != deflux::core::ProcessLifecycle::ProcessDetected
        || !deflux::core::is_valid_process_instance(observation.instance)) {
        return make_status(LiveObservationStatus::NotRunning,
                           "No verified GTA process is running.");
    }

    const EnvironmentPreflightResult preflight =
        scan_game_executable_directory(observation.instance.executable_path);
    if (!preflight.clean) {
        return make_status(LiveObservationStatus::EnvironmentContaminated, preflight.message);
    }

    const std::wstring path = detail::utf8_to_wide(observation.instance.executable_path);
    const auto fingerprint = detail::fingerprint_executable(path);
    if (!fingerprint.ok || !sha256_matches_live_observation_target(fingerprint.sha256_hex)) {
        return make_status(LiveObservationStatus::BuildMismatch,
                           "Executable SHA-256 does not match the legacy chain diagnostic target build.");
    }

    detail::ScopedProcess process;
    if (!process.open_read_only(observation.instance.process_id)) {
        return make_status(LiveObservationStatus::Error,
                           "OpenProcess(QUERY_LIMITED|VM_READ) failed.");
    }

    std::string identity_message;
    if (!verify_identity_on_handle(observation.instance, process, identity_message)) {
        return make_status(LiveObservationStatus::ProcessChanged, identity_message);
    }

    const auto module = detail::query_main_module(observation.instance.process_id, path);
    if (!module.found || module.base == 0 || module.size == 0) {
        return make_status(LiveObservationStatus::ModuleInvalid,
                           module.message.empty() ? std::string("Main module discovery failed.")
                                                  : module.message);
    }

    std::vector<std::uint8_t> headers(0x400, 0);
    if (!process.read_exact(module.base, headers.data(), headers.size())) {
        return make_status(LiveObservationStatus::ModuleInvalid,
                           "Failed to read PE headers from the main module.");
    }

    PeImageInfo pe{};
    PeParseStatus pe_status = parse_pe64_headers(headers.data(), headers.size(), pe);
    if (pe_status == PeParseStatus::Ok && pe.size_of_headers > headers.size() && pe.size_of_headers <= 0x2000u) {
        headers.assign(pe.size_of_headers, 0);
        if (!process.read_exact(module.base, headers.data(), headers.size())) {
            return make_status(LiveObservationStatus::ModuleInvalid,
                               "Failed to read full PE headers from the main module.");
        }
        pe_status = parse_pe64_headers(headers.data(), headers.size(), pe);
    }
    if (pe_status != PeParseStatus::Ok || pe.size_of_image != static_cast<std::uint32_t>(module.size)) {
        return make_status(LiveObservationStatus::ModuleInvalid,
                           "PE validation failed for legacy chain diagnostic.");
    }

    std::optional<std::uint64_t> game_state_va;
    if (config.sample_game_state_context) {
        std::vector<std::uint8_t> module_image;
        if (read_module_image(process, module.base, module.size, module_image)) {
            GameStateAddressResolution resolution{};
            if (resolve_ggame_state_from_module_image(module_image.data(), module_image.size(), resolution)
                && resolution.resolved) {
                game_state_va = module.base + resolution.global_rva;
            }
        }
    }

    LegacyChainDiagnosticSessionResult result{};
    result.process_id = observation.instance.process_id;
    result.module_base = module.base;
    result.module_size = module.size;
    result.status = LiveObservationStatus::Ok;
    result.message = "Legacy chain diagnostic session completed.";

    std::unordered_map<ProfileHistoryKey, LegacyChainTransformShape, ProfileHistoryKeyHash> movement_history;

    for (std::uint32_t sample_index = 0; sample_index < config.max_samples; ++sample_index) {
        LiveObservationPhase phase = LiveObservationPhase::Unknown;
        if (!next_phase(phase)) {
            break;
        }
        if (phase == LiveObservationPhase::Unknown) {
            result.message = "Unknown phase label; stopping legacy chain diagnostic session.";
            break;
        }

        if (!verify_identity_on_handle(observation.instance, process, identity_message)) {
            result.status = LiveObservationStatus::ProcessChanged;
            result.message = identity_message;
            break;
        }

        LegacyChainObservationSample sample{};
        sample.sample_index = sample_index;
        sample.timestamp_ms = steady_timestamp_ms();
        sample.phase = phase;
        sample.process_identity_valid = true;

        if (game_state_va.has_value()) {
            sample.game_state_sampled = true;
            std::uint8_t value = 0;
            sample.game_state_read_ok =
                process.read_exact(*game_state_va, &value, sizeof(value));
            if (sample.game_state_read_ok) {
                sample.raw_game_state = value;
            }
        }

        LegacyChainReadFn read_fn = [&process](std::uint64_t address, void* out, std::size_t bytes) {
            return process.read_exact(address, out, bytes);
        };

        for (std::uint32_t root_rva : kLegacyChainOpaqueRootRvas) {
            const std::uint64_t slot_va = module.base + root_rva;
            for (std::size_t profile_index = 0; profile_index < kLegacyChainProfiles.size(); ++profile_index) {
                const LegacyOffsetProfile& profile = kLegacyChainProfiles[profile_index];
                LegacyChainProfileSample profile_sample =
                    probe_legacy_chain_profile(slot_va, profile, read_fn);
                profile_sample.opaque_root_rva = root_rva;
                profile_sample.profile_index = static_cast<std::uint32_t>(profile_index);

                ProfileHistoryKey key{root_rva, static_cast<std::uint32_t>(profile_index)};
                const auto it = movement_history.find(key);
                if (it != movement_history.end()) {
                    profile_sample.movement = compute_legacy_chain_movement_delta(
                        it->second, profile_sample.transform, kLegacyChainMovementEpsilon);
                }
                if (profile_sample.transform.shape_valid) {
                    movement_history[key] = profile_sample.transform;
                }

                sample.profiles.push_back(profile_sample);
            }
        }

        result.samples.push_back(sample);
        ++result.samples_collected;

        if (!verify_identity_on_handle(observation.instance, process, identity_message)) {
            result.status = LiveObservationStatus::ProcessChanged;
            result.message = identity_message;
            break;
        }

        if (sample_index + 1 >= config.max_samples) {
            break;
        }
        sleep_interval_ms(config.interval_ms);
    }

    result.classification = classify_legacy_chain_session(result.samples);
    switch (result.classification) {
    case LegacyChainClassification::Observed:
        result.classification_rationale =
            "At least one legacy profile produced TRANSFORM_SHAPE_VALID evidence. "
            "This is not local-player identity.";
        break;
    case LegacyChainClassification::Contradicted:
        result.classification_rationale =
            "A previously shape-valid legacy chain later contradicted its own structural evidence.";
        break;
    case LegacyChainClassification::Insufficient:
        result.classification_rationale =
            result.samples.empty()
                ? "No labeled samples were collected."
                : "No legacy profile produced reproducible transform-shaped evidence.";
        break;
    }

    if (result.status == LiveObservationStatus::Ok && result.samples.empty()) {
        result.message = "Legacy chain diagnostic stopped before any labeled samples were collected.";
    }

    process.close();
    return result;
}

} // namespace deflux::game::gta_sa_de
