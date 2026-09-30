#include "deflux/game/gta_sa_de/adapter.hpp"
#include "deflux/game/gta_sa_de/in_process_research.hpp"
#include "deflux/game/gta_sa_de/in_process_research_test_seam.hpp"
#include "deflux/game/gta_sa_de/passive_same_process_read.hpp"
#include "deflux/game/gta_sa_de/research_build_allowlist.hpp"
#include "deflux/game/gta_sa_de/research_launcher.hpp"
#include "deflux/game/gta_sa_de/research_launcher_preflight.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdio>
#include <cstring>
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

#define CHECK_IP(expr) check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

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
        image[cfg.cmp_rva + 6] = 9;
    }
    return image;
}

deflux::game::gta_sa_de::InProcessResearchConfig make_enabled_config() {
    deflux::game::gta_sa_de::InProcessResearchConfig config{};
    config.research_enabled = true;
    config.experiment_id = deflux::game::gta_sa_de::kGGameStateInProcessExperimentId;
    config.experiment_version = deflux::game::gta_sa_de::kGGameStateInProcessExperimentVersion;
    config.capability = deflux::game::gta_sa_de::ResearchCapability::PassiveRead;
    config.expected_sha256 = deflux::game::gta_sa_de::kResearchAllowlistedSha256;
    config.interval_ms = deflux::game::gta_sa_de::kInProcessResearchMinIntervalMs;
    config.max_samples = 2;
    return config;
}

deflux::game::gta_sa_de::SameProcessModuleIdentity make_identity(
    const std::vector<std::uint8_t>& pe) {
    deflux::game::gta_sa_de::SameProcessModuleIdentity identity{};
    identity.module_image = pe.data();
    identity.module_image_size = pe.size();
    identity.module_base = reinterpret_cast<std::uintptr_t>(pe.data());
    identity.executable_path = "C:\\Synthetic\\SanAndreas.exe";
    identity.sha256_hex = deflux::game::gta_sa_de::kResearchAllowlistedSha256;
    identity.skip_environment_preflight = true;
    return identity;
}

void test_research_disabled_by_default() {
    using deflux::game::gta_sa_de::InProcessResearchController;
    using deflux::game::gta_sa_de::InProcessResearchConfig;
    using deflux::game::gta_sa_de::ResearchSubstate;

    InProcessResearchController controller{};
    CHECK_IP(controller.substate() == ResearchSubstate::Disabled);
    InProcessResearchConfig config{};
    std::string message;
    CHECK_IP(controller.configure(config, message));
    CHECK_IP(controller.substate() == ResearchSubstate::Disabled);
}

void test_allowlist_and_production_separation() {
    using deflux::game::gta_sa_de::is_production_supported_sha256;
    using deflux::game::gta_sa_de::is_research_allowlisted_sha256;
    using deflux::game::gta_sa_de::kResearchAllowlistedSha256;

    CHECK_IP(is_research_allowlisted_sha256(kResearchAllowlistedSha256));
    CHECK_IP(!is_research_allowlisted_sha256("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    CHECK_IP(!is_production_supported_sha256(kResearchAllowlistedSha256));
}

void test_experiment_and_capability_gates() {
    using deflux::game::gta_sa_de::InProcessResearchConfig;
    using deflux::game::gta_sa_de::ResearchCapability;
    using deflux::game::gta_sa_de::is_supported_in_process_experiment_id;
    using deflux::game::gta_sa_de::kGGameStateInProcessExperimentId;
    using deflux::game::gta_sa_de::normalize_in_process_research_config;
    using deflux::game::gta_sa_de::validate_research_capability;

    CHECK_IP(is_supported_in_process_experiment_id(kGGameStateInProcessExperimentId));
    CHECK_IP(!is_supported_in_process_experiment_id("UNKNOWN_EXPERIMENT"));

    InProcessResearchConfig config = make_enabled_config();
    std::string reason;
    CHECK_IP(normalize_in_process_research_config(config, reason));

    config.experiment_id = "UNKNOWN";
    CHECK_IP(!normalize_in_process_research_config(config, reason));

    config = make_enabled_config();
    config.capability = ResearchCapability::Write;
    CHECK_IP(!validate_research_capability(config.capability, reason));
    CHECK_IP(!normalize_in_process_research_config(config, reason));

    config = make_enabled_config();
    config.capability = ResearchCapability::Hook;
    CHECK_IP(!normalize_in_process_research_config(config, reason));
}

void test_interval_bounds() {
    using deflux::game::gta_sa_de::InProcessResearchConfig;
    using deflux::game::gta_sa_de::kInProcessResearchMaxIntervalMs;
    using deflux::game::gta_sa_de::kInProcessResearchMinIntervalMs;
    using deflux::game::gta_sa_de::normalize_in_process_research_config;

    InProcessResearchConfig config = make_enabled_config();
    std::string reason;
    config.interval_ms = kInProcessResearchMinIntervalMs - 1;
    CHECK_IP(!normalize_in_process_research_config(config, reason));
    config = make_enabled_config();
    config.interval_ms = kInProcessResearchMaxIntervalMs + 9999;
    CHECK_IP(normalize_in_process_research_config(config, reason));
    CHECK_IP(config.interval_ms == kInProcessResearchMaxIntervalMs);
}

void test_lifecycle_transitions() {
    using deflux::game::gta_sa_de::ResearchSubstate;
    using deflux::game::gta_sa_de::transition_research_substate;

    bool valid = false;
    CHECK_IP(transition_research_substate(ResearchSubstate::Disabled, ResearchSubstate::Armed, valid));
    CHECK_IP(valid);
    CHECK_IP(!transition_research_substate(ResearchSubstate::Disabled, ResearchSubstate::Observing, valid));
    CHECK_IP(!valid);
    CHECK_IP(transition_research_substate(ResearchSubstate::Armed, ResearchSubstate::Observing, valid));
    CHECK_IP(valid);
    CHECK_IP(transition_research_substate(ResearchSubstate::Observing, ResearchSubstate::Stopping, valid));
    CHECK_IP(valid);
    CHECK_IP(transition_research_substate(ResearchSubstate::Stopping, ResearchSubstate::Stopped, valid));
    CHECK_IP(valid);
}

void test_end_to_end_sample_fixture() {
    using deflux::game::gta_sa_de::GameStateAddressDerivation;
    using deflux::game::gta_sa_de::InProcessResearchController;
    using deflux::game::gta_sa_de::ResearchSubstate;
    using deflux::game::gta_sa_de::detail::in_process_research_test_wait_until_observing;

    SyntheticPeConfig cfg{};
    cfg.global_value = 7;
    const std::vector<std::uint8_t> pe = build_synthetic_pe(cfg);

    InProcessResearchController controller{};
    std::string message;
    CHECK_IP(controller.configure(make_enabled_config(), message));
    CHECK_IP(controller.arm(make_identity(pe), message));
    CHECK_IP(controller.begin_observation(message));
    CHECK_IP(in_process_research_test_wait_until_observing(controller, 2000));
    CHECK_IP(controller.stop_observation(message));
    const auto status = controller.status_snapshot();
    CHECK_IP(status.substate == ResearchSubstate::Stopped);
    CHECK_IP(status.samples.size() >= 1);
    CHECK_IP(status.samples.front().derivation_resolved);
    CHECK_IP(status.samples.front().derivation_method
             == GameStateAddressDerivation::MenuMainPresentsLeaCmpImm9);
    CHECK_IP(status.samples.front().read_ok);
    CHECK_IP(status.samples.front().raw_byte == 7);
    CHECK_IP(controller.shutdown(message));
}

void test_derivation_failure_fixture() {
    using deflux::game::gta_sa_de::InProcessResearchController;
    using deflux::game::gta_sa_de::ResearchSubstate;

    SyntheticPeConfig cfg{};
    cfg.include_anchor = false;
    const std::vector<std::uint8_t> pe = build_synthetic_pe(cfg);

    InProcessResearchController controller{};
    std::string message;
    CHECK_IP(controller.configure(make_enabled_config(), message));
    CHECK_IP(!controller.arm(make_identity(pe), message));
    CHECK_IP(controller.substate() == ResearchSubstate::Failed);
}

void test_passive_read_success_and_failure() {
    using deflux::game::gta_sa_de::passive_read_byte;

    std::uint8_t value = 9;
    const auto ok = passive_read_byte(&value);
    CHECK_IP(ok.ok);
    CHECK_IP(ok.byte == 9);

    const auto bad = passive_read_byte(reinterpret_cast<const void*>(0x1));
    CHECK_IP(!bad.ok);
}

void test_shutdown_and_stop() {
    using deflux::game::gta_sa_de::InProcessResearchController;
    using deflux::game::gta_sa_de::ResearchSubstate;

    SyntheticPeConfig cfg{};
    const std::vector<std::uint8_t> pe = build_synthetic_pe(cfg);
    InProcessResearchController controller{};
    std::string message;
    CHECK_IP(controller.configure(make_enabled_config(), message));
    CHECK_IP(controller.arm(make_identity(pe), message));
    CHECK_IP(controller.stop_observation(message));
    CHECK_IP(controller.substate() == ResearchSubstate::Stopped);
    CHECK_IP(controller.shutdown(message));
    CHECK_IP(controller.substate() == ResearchSubstate::Disabled);
}

void test_async_observing_shutdown() {
    using deflux::game::gta_sa_de::InProcessResearchController;
    using deflux::game::gta_sa_de::ResearchSubstate;
    using deflux::game::gta_sa_de::detail::in_process_research_test_wait_until_observing;

    SyntheticPeConfig cfg{};
    const std::vector<std::uint8_t> pe = build_synthetic_pe(cfg);
    InProcessResearchController controller{};
    std::string message;
    auto config = make_enabled_config();
    config.max_samples = 120;
    CHECK_IP(controller.configure(config, message));
    CHECK_IP(controller.arm(make_identity(pe), message));
    CHECK_IP(controller.begin_observation(message));
    CHECK_IP(in_process_research_test_wait_until_observing(controller, 2000));
    CHECK_IP(controller.substate() == ResearchSubstate::Observing);
    CHECK_IP(controller.status_snapshot().samples_collected >= 1);
    CHECK_IP(controller.shutdown(message));
    CHECK_IP(controller.substate() == ResearchSubstate::Disabled);
}

void test_async_sampler_read_failure_shutdown() {
    using deflux::game::gta_sa_de::InProcessResearchController;
    using deflux::game::gta_sa_de::ResearchSubstate;
    using deflux::game::gta_sa_de::detail::in_process_research_test_force_derived_address;
    using deflux::game::gta_sa_de::detail::in_process_research_test_wait_until_observing;
    using deflux::game::gta_sa_de::detail::in_process_research_test_wait_until_substate;

    SyntheticPeConfig cfg{};
    const std::vector<std::uint8_t> pe = build_synthetic_pe(cfg);
    InProcessResearchController controller{};
    std::string message;
    auto config = make_enabled_config();
    config.max_samples = 120;
    CHECK_IP(controller.configure(config, message));
    CHECK_IP(controller.arm(make_identity(pe), message));
    CHECK_IP(controller.begin_observation(message));
    CHECK_IP(in_process_research_test_wait_until_observing(controller, 2000));
    CHECK_IP(in_process_research_test_force_derived_address(controller, 0x1));
    CHECK_IP(in_process_research_test_wait_until_substate(controller, ResearchSubstate::Failed, 3000));
    CHECK_IP(controller.shutdown(message));
    CHECK_IP(controller.substate() == ResearchSubstate::Failed);
}

void test_adapter_readiness_unchanged() {
    using deflux::game::gta_sa_de::GtaSaDeAdapter;
    using deflux::game::Readiness;

    const GtaSaDeAdapter adapter;
    CHECK_IP(adapter.engine_ready().status == Readiness::NotImplemented);
    CHECK_IP(adapter.world_ready().status == Readiness::NotImplemented);
    CHECK_IP(adapter.local_player_ready().status == Readiness::NotImplemented);
    CHECK_IP(adapter.probe_supported_game().status
             != deflux::game::ProbeStatus::Supported);
}

void test_research_launcher_inject_blocked() {
    using deflux::game::gta_sa_de::run_research_launcher;

    char arg0[] = "DeFlux_ResearchLauncher.exe";
    char arg1[] = "--inject";
    char arg2[] = "--pid";
    char arg3[] = "1234";
    char* argv[] = {arg0, arg1, arg2, arg3};
    const auto result = run_research_launcher(4, argv);
    CHECK_IP(!result.ok);
    CHECK_IP(result.inject_requested);
}

void test_auto_stop_leaves_no_joinable_sampler_thread() {
    using deflux::game::gta_sa_de::InProcessResearchController;
    using deflux::game::gta_sa_de::ResearchSubstate;
    using deflux::game::gta_sa_de::detail::in_process_research_test_sampler_thread_joinable;
    using deflux::game::gta_sa_de::detail::in_process_research_test_wait_until_substate;

    SyntheticPeConfig cfg{};
    const std::vector<std::uint8_t> pe = build_synthetic_pe(cfg);
    InProcessResearchController controller{};
    std::string message;
    auto config = make_enabled_config();
    config.max_samples = 2;
    config.interval_ms = 50;
    CHECK_IP(controller.configure(config, message));
    CHECK_IP(controller.arm(make_identity(pe), message));
    CHECK_IP(controller.begin_observation(message));
    CHECK_IP(in_process_research_test_wait_until_substate(controller, ResearchSubstate::Stopped, 3000));
    CHECK_IP(controller.shutdown(message));
    CHECK_IP(!in_process_research_test_sampler_thread_joinable(controller));
}

void test_sample_cap_exact(std::uint32_t max_samples) {
    using deflux::game::gta_sa_de::InProcessResearchController;
    using deflux::game::gta_sa_de::ResearchSubstate;
    using deflux::game::gta_sa_de::detail::in_process_research_test_wait_until_substate;

    SyntheticPeConfig cfg{};
    const std::vector<std::uint8_t> pe = build_synthetic_pe(cfg);
    InProcessResearchController controller{};
    std::string message;
    auto config = make_enabled_config();
    config.max_samples = max_samples;
    config.interval_ms = 50;
    CHECK_IP(controller.configure(config, message));
    CHECK_IP(controller.arm(make_identity(pe), message));
    CHECK_IP(controller.begin_observation(message));
    CHECK_IP(in_process_research_test_wait_until_substate(
        controller, ResearchSubstate::Stopped, 5000));
    CHECK_IP(controller.status_snapshot().samples_collected == max_samples);
    CHECK_IP(controller.shutdown(message));
}

void test_sample_limit_enforcement() {
    using deflux::game::gta_sa_de::InProcessResearchController;
    using deflux::game::gta_sa_de::ResearchSubstate;
    using deflux::game::gta_sa_de::detail::in_process_research_test_wait_until_substate;

    SyntheticPeConfig cfg{};
    const std::vector<std::uint8_t> pe = build_synthetic_pe(cfg);
    InProcessResearchController controller{};
    std::string message;
    auto config = make_enabled_config();
    config.max_samples = 1;
    CHECK_IP(controller.configure(config, message));
    CHECK_IP(controller.arm(make_identity(pe), message));
    CHECK_IP(controller.begin_observation(message));
    CHECK_IP(in_process_research_test_wait_until_substate(controller, ResearchSubstate::Stopped, 2000));
    CHECK_IP(controller.status_snapshot().samples_collected == 1);
    CHECK_IP(controller.shutdown(message));
}

} // namespace

int run_qualification_in_process_research_tests(int& passed, int& failed) {
    test_research_disabled_by_default();
    test_allowlist_and_production_separation();
    test_experiment_and_capability_gates();
    test_interval_bounds();
    test_lifecycle_transitions();
    test_end_to_end_sample_fixture();
    test_derivation_failure_fixture();
    test_passive_read_success_and_failure();
    test_shutdown_and_stop();
    test_async_observing_shutdown();
    test_async_sampler_read_failure_shutdown();
    test_adapter_readiness_unchanged();
    test_research_launcher_inject_blocked();
    test_sample_limit_enforcement();
    test_sample_cap_exact(1);
    test_sample_cap_exact(2);
    test_sample_cap_exact(5);
    test_auto_stop_leaves_no_joinable_sampler_thread();
    passed += g_passed;
    failed += g_failed;
    return g_failed == 0 ? 0 : 1;
}

} // namespace deflux_tests
