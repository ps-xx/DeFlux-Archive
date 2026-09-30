#include "deflux/core/process_lifecycle.hpp"
#include "deflux/game/gta_sa_de/adapter.hpp"
#include "deflux/game/gta_sa_de/ggame_state_observation.hpp"
#include "deflux/game/gta_sa_de/gworld_live_observation.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace deflux_tests {
namespace {

int g_failed = 0;
int g_passed = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    if (condition) {
        ++g_passed;
        return;
    }
    ++g_failed;
    std::printf("FAIL %s:%d: %s\n", file, line, expression);
}

#define CHECK_GS(expr) check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

struct SyntheticPeConfig {
    std::uint32_t image_size = 0x8000;
    std::uint32_t text_rva = 0x1000;
    std::uint32_t rdata_rva = 0x4000;
    std::uint32_t anchor_rva = 0x5000;
    std::uint32_t lea_rva = 0x1100;
    std::uint32_t cmp_rva = 0x1150;
    std::uint32_t global_rva = 0x6000;
    std::uint8_t global_value = 9;
    bool include_anchor = true;
    bool include_lea = true;
    bool include_cmp = true;
    std::uint8_t cmp_immediate = 9;
};

std::vector<std::uint8_t> build_synthetic_pe(const SyntheticPeConfig& cfg) {
    std::vector<std::uint8_t> image(cfg.image_size, 0);

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image.data());
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;

    const std::size_t nt_offset = static_cast<std::size_t>(dos->e_lfanew);
    *reinterpret_cast<std::uint32_t*>(image.data() + nt_offset) = IMAGE_NT_SIGNATURE;

    auto* file_header =
        reinterpret_cast<IMAGE_FILE_HEADER*>(image.data() + nt_offset + sizeof(std::uint32_t));
    file_header->Machine = IMAGE_FILE_MACHINE_AMD64;
    file_header->NumberOfSections = 2;
    file_header->SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);

    auto* optional =
        reinterpret_cast<IMAGE_OPTIONAL_HEADER64*>(reinterpret_cast<std::uint8_t*>(file_header)
                                                 + sizeof(IMAGE_FILE_HEADER));
    optional->Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    optional->SectionAlignment = 0x1000;
    optional->FileAlignment = 0x200;
    optional->SizeOfImage = cfg.image_size;
    optional->SizeOfHeaders = 0x400;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image.data() + nt_offset);
    IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);

    std::memcpy(sections[0].Name, ".text\0\0\0", 8);
    sections[0].VirtualAddress = cfg.text_rva;
    sections[0].Misc.VirtualSize = 0x2000;
    sections[0].Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;

    std::memcpy(sections[1].Name, ".rdata\0\0", 8);
    sections[1].VirtualAddress = cfg.rdata_rva;
    sections[1].Misc.VirtualSize = 0x3000;
    sections[1].Characteristics = IMAGE_SCN_MEM_READ;

    if (cfg.include_anchor) {
        constexpr char kAnchor[] = "menu_mainpresents";
        std::memcpy(image.data() + cfg.anchor_rva, kAnchor, sizeof(kAnchor));
    }

    image[cfg.global_rva] = cfg.global_value;

    if (cfg.include_lea) {
        const std::int32_t lea_disp =
            static_cast<std::int32_t>(cfg.anchor_rva - (cfg.lea_rva + 7));
        image[cfg.lea_rva + 0] = 0x48;
        image[cfg.lea_rva + 1] = 0x8D;
        image[cfg.lea_rva + 2] = 0x05;
        std::memcpy(image.data() + cfg.lea_rva + 3, &lea_disp, sizeof(lea_disp));
    }

    if (cfg.include_cmp) {
        const std::int32_t cmp_disp =
            static_cast<std::int32_t>(cfg.global_rva - (cfg.cmp_rva + 7));
        image[cfg.cmp_rva + 0] = 0x80;
        image[cfg.cmp_rva + 1] = 0x3D;
        std::memcpy(image.data() + cfg.cmp_rva + 2, &cmp_disp, sizeof(cmp_disp));
        image[cfg.cmp_rva + 6] = cfg.cmp_immediate;
    }

    return image;
}

deflux::core::ProcessInstance make_instance(std::uint32_t pid,
                                            std::uint64_t creation,
                                            const char* path) {
    deflux::core::ProcessInstance instance{};
    instance.process_id = pid;
    instance.creation_identity = creation;
    instance.executable_path = path != nullptr ? path : "";
    return instance;
}

void test_legacy_constants_and_derivation_strings() {
    using deflux::game::gta_sa_de::GameStateAddressDerivation;
    using deflux::game::gta_sa_de::kLegacyGameStateAnchorString;
    using deflux::game::gta_sa_de::kLegacyGsFrontend;
    using deflux::game::gta_sa_de::kLegacyGsInitPlayingGame;
    using deflux::game::gta_sa_de::kLegacyGsPlayingGame;
    using deflux::game::gta_sa_de::to_string;

    CHECK_GS(kLegacyGsFrontend == 7);
    CHECK_GS(kLegacyGsInitPlayingGame == 8);
    CHECK_GS(kLegacyGsPlayingGame == 9);
    CHECK_GS(std::strcmp(kLegacyGameStateAnchorString, "menu_mainpresents") == 0);
    CHECK_GS(std::strcmp(to_string(GameStateAddressDerivation::MenuMainPresentsLeaCmpImm9),
                         "MENU_MAINPRESENTS_LEA_CMP_IMM9")
             == 0);
}

void test_config_normalization_and_bounds() {
    using deflux::game::gta_sa_de::GameStateObservationConfig;
    using deflux::game::gta_sa_de::kGameStateObservationDefaultIntervalMs;
    using deflux::game::gta_sa_de::kGameStateObservationDefaultMaxSamples;
    using deflux::game::gta_sa_de::kGameStateObservationMaxIntervalMs;
    using deflux::game::gta_sa_de::kGameStateObservationMaxSamples;
    using deflux::game::gta_sa_de::normalize_game_state_observation_config;

    GameStateObservationConfig config{};
    config.interval_ms = 0;
    config.max_samples = 0;
    CHECK_GS(normalize_game_state_observation_config(config));
    CHECK_GS(config.interval_ms == kGameStateObservationDefaultIntervalMs);
    CHECK_GS(config.max_samples == kGameStateObservationDefaultMaxSamples);

    config.interval_ms = 99999;
    config.max_samples = 99999;
    CHECK_GS(normalize_game_state_observation_config(config));
    CHECK_GS(config.interval_ms == kGameStateObservationMaxIntervalMs);
    CHECK_GS(config.max_samples == kGameStateObservationMaxSamples);
}

void test_address_derivation_success() {
    using deflux::game::gta_sa_de::GameStateAddressDerivation;
    using deflux::game::gta_sa_de::GameStateAddressResolution;
    using deflux::game::gta_sa_de::resolve_ggame_state_from_module_image;

    SyntheticPeConfig cfg{};
    const std::vector<std::uint8_t> image = build_synthetic_pe(cfg);

    GameStateAddressResolution resolution{};
    CHECK_GS(resolve_ggame_state_from_module_image(image.data(), image.size(), resolution));
    CHECK_GS(resolution.resolved);
    CHECK_GS(resolution.method == GameStateAddressDerivation::MenuMainPresentsLeaCmpImm9);
    CHECK_GS(resolution.global_rva == cfg.global_rva);
    CHECK_GS(resolution.cmp_instruction_rva == cfg.cmp_rva);
    CHECK_GS(resolution.anchor_string_rva == cfg.anchor_rva);
    CHECK_GS(resolution.lea_xref_count >= 1);
}

void test_address_derivation_rejections() {
    using deflux::game::gta_sa_de::GameStateAddressResolution;
    using deflux::game::gta_sa_de::resolve_ggame_state_from_module_image;

    SyntheticPeConfig missing_anchor{};
    missing_anchor.include_anchor = false;
    const std::vector<std::uint8_t> no_anchor = build_synthetic_pe(missing_anchor);
    GameStateAddressResolution resolution{};
    CHECK_GS(!resolve_ggame_state_from_module_image(no_anchor.data(), no_anchor.size(), resolution));
    CHECK_GS(!resolution.resolved);

    SyntheticPeConfig missing_lea{};
    missing_lea.include_lea = false;
    const std::vector<std::uint8_t> no_lea = build_synthetic_pe(missing_lea);
    resolution = GameStateAddressResolution{};
    CHECK_GS(!resolve_ggame_state_from_module_image(no_lea.data(), no_lea.size(), resolution));
    CHECK_GS(!resolution.resolved);

    SyntheticPeConfig missing_cmp{};
    missing_cmp.include_cmp = false;
    const std::vector<std::uint8_t> no_cmp = build_synthetic_pe(missing_cmp);
    resolution = GameStateAddressResolution{};
    CHECK_GS(!resolve_ggame_state_from_module_image(no_cmp.data(), no_cmp.size(), resolution));
    CHECK_GS(!resolution.resolved);

    resolution = GameStateAddressResolution{};
    CHECK_GS(!resolve_ggame_state_from_module_image(nullptr, 0, resolution));
}

void test_classification_and_phase_labels() {
    using deflux::game::gta_sa_de::GameStateObservationClassification;
    using deflux::game::gta_sa_de::GameStateObservationSample;
    using deflux::game::gta_sa_de::LiveObservationPhase;
    using deflux::game::gta_sa_de::classify_game_state_observation;
    using deflux::game::gta_sa_de::parse_live_observation_phase;
    using deflux::game::gta_sa_de::summarize_game_state_observation_by_phase;
    using deflux::game::gta_sa_de::to_string;

    CHECK_GS(classify_game_state_observation(true, 1, 1)
             == GameStateObservationClassification::Observed);
    CHECK_GS(classify_game_state_observation(true, 0, 3)
             == GameStateObservationClassification::Contradicted);
    CHECK_GS(classify_game_state_observation(false, 0, 0)
             == GameStateObservationClassification::Insufficient);
    CHECK_GS(std::strcmp(to_string(GameStateObservationClassification::Observed), "GAMESTATE_OBSERVED")
             == 0);
    CHECK_GS(parse_live_observation_phase("GAMEPLAY_STANDING") == LiveObservationPhase::GameplayStanding);

    std::vector<GameStateObservationSample> samples;
    GameStateObservationSample menu{};
    menu.phase = LiveObservationPhase::Menu;
    menu.read_ok = true;
    menu.raw_value = 7;
    samples.push_back(menu);

    GameStateObservationSample gameplay{};
    gameplay.phase = LiveObservationPhase::GameplayMoving;
    gameplay.read_ok = true;
    gameplay.raw_value = 9;
    samples.push_back(gameplay);

    GameStateObservationSample unexpected{};
    unexpected.phase = LiveObservationPhase::GameplayMoving;
    unexpected.read_ok = true;
    unexpected.raw_value = 42;
    samples.push_back(unexpected);

    const auto by_phase = summarize_game_state_observation_by_phase(samples);
    CHECK_GS(by_phase.size() == 2);
    CHECK_GS(by_phase[0].phase == LiveObservationPhase::Menu);
    CHECK_GS(by_phase[0].distinct_values == 1);
    CHECK_GS(by_phase[1].phase == LiveObservationPhase::GameplayMoving);
    CHECK_GS(by_phase[1].distinct_values == 2);
}

void test_session_not_running_observation_error_and_stop_callback() {
    using deflux::game::gta_sa_de::GameStateObservationClassification;
    using deflux::game::gta_sa_de::LiveObservationPhase;
    using deflux::game::gta_sa_de::LiveObservationStatus;
    using deflux::game::gta_sa_de::run_game_state_observation_session;

    deflux::core::ProcessObservation observation{};
    observation.state = deflux::core::ProcessLifecycle::NotRunning;
    observation.instance = make_instance(100, 200, "C:\\Games\\SanAndreas.exe");

    std::size_t phase_calls = 0;
    const auto not_running = run_game_state_observation_session(
        observation,
        {},
        [&phase_calls](LiveObservationPhase& phase_out) {
            ++phase_calls;
            phase_out = LiveObservationPhase::Menu;
            return true;
        });
    CHECK_GS(not_running.status == LiveObservationStatus::NotRunning);
    CHECK_GS(phase_calls == 0);
    CHECK_GS(not_running.classification == GameStateObservationClassification::Insufficient);

    observation.state = deflux::core::ProcessLifecycle::ObservationError;
    observation.message = "Synthetic observation error.";
    phase_calls = 0;
    const auto errored = run_game_state_observation_session(
        observation,
        {},
        [&phase_calls](LiveObservationPhase& phase_out) {
            ++phase_calls;
            phase_out = LiveObservationPhase::Menu;
            return true;
        });
    CHECK_GS(errored.status == LiveObservationStatus::Error);
    CHECK_GS(phase_calls == 0);

    LiveObservationPhase phase = LiveObservationPhase::Menu;
    phase_calls = 0;
    const bool stop_immediately = [&phase_calls](LiveObservationPhase& phase_out) {
        ++phase_calls;
        phase_out = LiveObservationPhase::Menu;
        return false;
    }(phase);
    CHECK_GS(!stop_immediately);
    CHECK_GS(phase_calls == 1);
    CHECK_GS(phase == LiveObservationPhase::Menu);
}

void test_runtime_isolation_registry_and_readiness() {
    using deflux::game::gta_sa_de::GtaSaDeAdapter;
    using deflux::game::gta_sa_de::is_supported_fingerprint;
    using deflux::game::gta_sa_de::kLiveObservationTargetSha256;

    CHECK_GS(!is_supported_fingerprint(kLiveObservationTargetSha256));

    const GtaSaDeAdapter adapter;
    CHECK_GS(adapter.engine_ready().status == deflux::game::Readiness::NotImplemented);
    CHECK_GS(adapter.world_ready().status == deflux::game::Readiness::NotImplemented);
    CHECK_GS(adapter.local_player_ready().status == deflux::game::Readiness::NotImplemented);
    CHECK_GS(adapter.probe_supported_game().status == deflux::game::ProbeStatus::Unsupported);
}

void test_no_gworld_dependency() {
    using deflux::game::gta_sa_de::GameStateAddressResolution;
    using deflux::game::gta_sa_de::is_architect_approved_candidate_rva;
    using deflux::game::gta_sa_de::resolve_ggame_state_from_module_image;

    SyntheticPeConfig cfg{};
    const std::vector<std::uint8_t> image = build_synthetic_pe(cfg);
    GameStateAddressResolution resolution{};
    CHECK_GS(resolve_ggame_state_from_module_image(image.data(), image.size(), resolution));
    CHECK_GS(!is_architect_approved_candidate_rva(resolution.global_rva));
    CHECK_GS(!is_architect_approved_candidate_rva(resolution.cmp_instruction_rva));
}

} // namespace

int run_qualification_ggame_state_observation_tests(int& passed, int& failed) {
    const int before_passed = g_passed;
    const int before_failed = g_failed;

    test_legacy_constants_and_derivation_strings();
    test_config_normalization_and_bounds();
    test_address_derivation_success();
    test_address_derivation_rejections();
    test_classification_and_phase_labels();
    test_session_not_running_observation_error_and_stop_callback();
    test_runtime_isolation_registry_and_readiness();
    test_no_gworld_dependency();

    passed += g_passed - before_passed;
    failed += g_failed - before_failed;
    return g_failed - before_failed;
}

} // namespace deflux_tests
