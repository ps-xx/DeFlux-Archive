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
#include <cstring>
#include <thread>
#include <unordered_set>

namespace deflux::game::gta_sa_de {
namespace {

constexpr std::size_t kLegacyXrefScanWindow = 0x800;
constexpr std::size_t kMaxLeaXrefs = 24;

bool rva_readable(std::size_t image_size, std::uintptr_t rva, std::size_t bytes) noexcept {
    return bytes > 0 && rva + bytes <= image_size;
}

std::uintptr_t find_ascii_in_image(const std::uint8_t* base,
                                   std::size_t image_size,
                                   const char* needle) {
    if (base == nullptr || needle == nullptr) {
        return 0;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (!rva_readable(image_size, 0, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return 0;
    }
    if (static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > image_size) {
        return 0;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return 0;
    }
    const std::size_t nlen = std::strlen(needle);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (std::uint16_t s = 0; s < nt->FileHeader.NumberOfSections; ++s) {
        if ((sec[s].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) {
            continue;
        }
        const std::uintptr_t begin = sec[s].VirtualAddress;
        const std::size_t span = sec[s].Misc.VirtualSize;
        if (span <= nlen || !rva_readable(image_size, begin, nlen)) {
            continue;
        }
        const auto* bytes = base + begin;
        for (std::size_t i = 0; i + nlen < span; ++i) {
            if (std::memcmp(bytes + i, needle, nlen) == 0) {
                return begin + i;
            }
        }
    }
    return 0;
}

void collect_lea_xrefs(const std::uint8_t* base,
                         std::size_t image_size,
                         std::uintptr_t target,
                         std::vector<std::uintptr_t>& out) {
    if (base == nullptr) {
        return;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (!rva_readable(image_size, 0, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return;
    }
    if (static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > image_size) {
        return;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return;
    }
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (std::uint16_t s = 0; s < nt->FileHeader.NumberOfSections; ++s) {
        if ((sec[s].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
            continue;
        }
        const std::uintptr_t begin = sec[s].VirtualAddress;
        const std::size_t span = sec[s].Misc.VirtualSize;
        if (span < 7) {
            continue;
        }
        const auto* bytes = base + begin;
        for (std::size_t i = 0; i + 7 <= span && out.size() < kMaxLeaXrefs; ++i) {
            if ((bytes[i] == 0x48 || bytes[i] == 0x4C) && bytes[i + 1] == 0x8D
                && (bytes[i + 2] & 0xC7u) == 0x05u) {
                std::int32_t disp = 0;
                std::memcpy(&disp, bytes + i + 3, 4);
                const std::uintptr_t abs = begin + i + 7 + static_cast<std::intptr_t>(disp);
                if (abs == target) {
                    out.push_back(begin + i);
                }
            }
        }
    }
}

GameStateObservationSessionResult make_status(LiveObservationStatus status, std::string message) {
    GameStateObservationSessionResult result{};
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
        message_out = "Process identity changed during gGameState observation; aborting.";
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

} // namespace

const char* to_string(GameStateAddressDerivation derivation) noexcept {
    switch (derivation) {
    case GameStateAddressDerivation::None:
        return "NONE";
    case GameStateAddressDerivation::MenuMainPresentsLeaCmpImm9:
        return "MENU_MAINPRESENTS_LEA_CMP_IMM9";
    }
    return "UNKNOWN";
}

const char* to_string(GameStateObservationClassification classification) noexcept {
    switch (classification) {
    case GameStateObservationClassification::Observed:
        return "GAMESTATE_OBSERVED";
    case GameStateObservationClassification::Insufficient:
        return "GAMESTATE_INSUFFICIENT";
    case GameStateObservationClassification::Contradicted:
        return "GAMESTATE_CONTRADICTED";
    }
    return "UNKNOWN";
}

bool normalize_game_state_observation_config(GameStateObservationConfig& config) noexcept {
    if (config.expected_sha256 == nullptr || config.expected_sha256[0] == '\0') {
        config.expected_sha256 = kLiveObservationTargetSha256;
    }
    if (config.interval_ms == 0) {
        config.interval_ms = kGameStateObservationDefaultIntervalMs;
    }
    if (config.max_samples == 0) {
        config.max_samples = kGameStateObservationDefaultMaxSamples;
    }
    if (config.interval_ms > kGameStateObservationMaxIntervalMs) {
        config.interval_ms = kGameStateObservationMaxIntervalMs;
    }
    if (config.max_samples > kGameStateObservationMaxSamples) {
        config.max_samples = kGameStateObservationMaxSamples;
    }
    return sha256_matches_live_observation_target(config.expected_sha256);
}

bool resolve_ggame_state_from_module_image(const std::uint8_t* image,
                                           std::size_t image_size,
                                           GameStateAddressResolution& out) noexcept {
    out = GameStateAddressResolution{};
    if (image == nullptr || image_size < 0x1000) {
        out.message = "Module image buffer is too small for gGameState derivation.";
        return false;
    }

    const std::uintptr_t anchor_rva =
        find_ascii_in_image(image, image_size, kLegacyGameStateAnchorString);
    if (anchor_rva == 0) {
        out.message =
            "Anchor string \"menu_mainpresents\" not found; legacy derivation cannot proceed.";
        return false;
    }
    out.anchor_string_rva = static_cast<std::uint32_t>(anchor_rva);

    std::vector<std::uintptr_t> xrefs;
    collect_lea_xrefs(image, image_size, anchor_rva, xrefs);
    out.lea_xref_count = xrefs.size();
    if (xrefs.empty()) {
        out.message =
            "No RIP-relative LEA xrefs to \"menu_mainpresents\" found; legacy derivation blocked.";
        return false;
    }

    for (std::uintptr_t xref : xrefs) {
        const std::uintptr_t begin =
            (xref > kLegacyXrefScanWindow) ? (xref - kLegacyXrefScanWindow) : 0;
        const std::uintptr_t end = xref + kLegacyXrefScanWindow;
        for (std::uintptr_t ip = begin; ip + 8 <= end; ++ip) {
            if (!rva_readable(image_size, ip, 8)) {
                continue;
            }
            const auto* b = image + ip;
            if ((b[0] == 0x80 || b[0] == 0x83) && b[1] == 0x3D && b[6] == kLegacyGsCmpImmediate) {
                std::int32_t disp = 0;
                std::memcpy(&disp, b + 2, 4);
                const std::uintptr_t global_rva =
                    ip + 7 + static_cast<std::intptr_t>(disp);
                if (!rva_readable(image_size, global_rva, 1)) {
                    continue;
                }
                out.resolved = true;
                out.method = GameStateAddressDerivation::MenuMainPresentsLeaCmpImm9;
                out.global_rva = static_cast<std::uint32_t>(global_rva);
                out.cmp_instruction_rva = static_cast<std::uint32_t>(ip);
                out.message =
                    "gGameState address derived via legacy menu_mainpresents LEA xref and "
                    "cmp byte ptr [rip+disp], imm8==9 pattern.";
                return true;
            }
        }
    }

    out.message =
        "Legacy cmp byte ptr [rip+disp], 9 pattern not found near menu_mainpresents xrefs.";
    return false;
}

GameStateObservationClassification classify_game_state_observation(
    bool address_resolved,
    std::size_t successful_reads,
    std::size_t read_attempts) noexcept {
    if (!address_resolved) {
        return GameStateObservationClassification::Insufficient;
    }
    if (successful_reads >= 1) {
        return GameStateObservationClassification::Observed;
    }
    if (read_attempts >= 1) {
        return GameStateObservationClassification::Contradicted;
    }
    return GameStateObservationClassification::Insufficient;
}

std::vector<GameStatePhaseSummary> summarize_game_state_observation_by_phase(
    const std::vector<GameStateObservationSample>& samples) noexcept {
    std::vector<GameStatePhaseSummary> summaries;
    static constexpr LiveObservationPhase kPhases[] = {
        LiveObservationPhase::Menu,
        LiveObservationPhase::Loading,
        LiveObservationPhase::GameplayStanding,
        LiveObservationPhase::GameplayMoving,
        LiveObservationPhase::PauseMenu,
    };

    for (LiveObservationPhase phase : kPhases) {
        GameStatePhaseSummary summary{};
        summary.phase = phase;
        std::unordered_set<std::uint8_t> distinct;
        for (const GameStateObservationSample& sample : samples) {
            if (sample.phase != phase) {
                continue;
            }
            ++summary.samples;
            if (sample.read_ok) {
                ++summary.read_successes;
                distinct.insert(sample.raw_value);
            }
        }
        summary.distinct_values = distinct.size();
        if (summary.samples > 0) {
            summaries.push_back(summary);
        }
    }
    return summaries;
}

GameStateObservationSessionResult run_game_state_observation_session(
    const deflux::core::ProcessObservation& observation,
    GameStateObservationConfig config,
    const std::function<bool(LiveObservationPhase& phase_out)>& next_phase) {
    if (!normalize_game_state_observation_config(config)) {
        return make_status(LiveObservationStatus::BuildMismatch,
                           "gGameState observation requires the architect-approved target SHA-256.");
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
                           "Executable SHA-256 does not match the gGameState observation target build.");
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
                           "PE validation failed for gGameState observation.");
    }

    std::vector<std::uint8_t> module_image;
    if (!read_module_image(process, module.base, module.size, module_image)) {
        return make_status(LiveObservationStatus::ModuleInvalid,
                           "Failed to read the main module image for legacy gGameState derivation.");
    }

    GameStateObservationSessionResult result{};
    result.process_id = observation.instance.process_id;
    result.module_base = module.base;
    result.module_size = module.size;

    if (!resolve_ggame_state_from_module_image(module_image.data(), module_image.size(), result.resolution)) {
        result.status = LiveObservationStatus::Ok;
        result.message =
            "gGameState observation stopped: legacy address derivation failed for this build.";
        result.classification = GameStateObservationClassification::Insufficient;
        result.classification_rationale = result.resolution.message;
        process.close();
        return result;
    }

    const std::uint64_t global_va = module.base + result.resolution.global_rva;

    result.status = LiveObservationStatus::Ok;
    result.message = "gGameState observation session completed.";
    std::size_t successful_reads = 0;
    std::size_t read_attempts = 0;

    for (std::uint32_t sample_index = 0; sample_index < config.max_samples; ++sample_index) {
        LiveObservationPhase phase = LiveObservationPhase::Unknown;
        if (!next_phase(phase)) {
            break;
        }
        if (phase == LiveObservationPhase::Unknown) {
            result.message = "Unknown phase label; stopping gGameState observation session.";
            break;
        }

        bool identity_valid = verify_identity_on_handle(observation.instance, process, identity_message);
        if (!identity_valid) {
            result.status = LiveObservationStatus::ProcessChanged;
            result.message = identity_message;
            break;
        }

        GameStateObservationSample sample{};
        sample.sample_index = sample_index;
        sample.timestamp_ms = steady_timestamp_ms();
        sample.phase = phase;
        sample.derivation_method = result.resolution.method;
        sample.global_rva = result.resolution.global_rva;
        sample.global_va = global_va;
        sample.process_identity_valid = identity_valid;

        std::uint8_t value = 0;
        ++read_attempts;
        sample.read_ok = process.read_exact(global_va, &value, sizeof(value));
        if (sample.read_ok) {
            sample.raw_value = value;
            ++successful_reads;
        }
        result.samples.push_back(sample);

        if (!verify_identity_on_handle(observation.instance, process, identity_message)) {
            result.status = LiveObservationStatus::ProcessChanged;
            result.message = identity_message;
            break;
        }

        ++result.samples_collected;
        if (sample_index + 1 >= config.max_samples) {
            break;
        }
        sleep_interval_ms(config.interval_ms);
    }

    result.by_phase = summarize_game_state_observation_by_phase(result.samples);
    result.classification = classify_game_state_observation(
        result.resolution.resolved, successful_reads, read_attempts);
    switch (result.classification) {
    case GameStateObservationClassification::Observed:
        result.classification_rationale =
            "Legacy-derived gGameState address produced at least one successful read.";
        break;
    case GameStateObservationClassification::Contradicted:
        result.classification_rationale =
            "Legacy-derived gGameState address was unreadable during sampling.";
        break;
    case GameStateObservationClassification::Insufficient:
        result.classification_rationale = result.resolution.resolved
            ? "No successful gGameState reads were collected."
            : result.resolution.message;
        break;
    }

    if (result.status == LiveObservationStatus::Ok && result.samples.empty()) {
        result.message = "Observation session stopped before any labeled samples were collected.";
    }

    process.close();
    return result;
}

} // namespace deflux::game::gta_sa_de
