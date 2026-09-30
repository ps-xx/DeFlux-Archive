#include "in_process_research_host.hpp"
#include "research_activation.hpp"

#include "deflux/game/gta_sa_de/in_process_research_test_seam.hpp"
#include "deflux/game/gta_sa_de/research_build_allowlist.hpp"
#include "deflux/game/gta_sa_de/research_evidence_log.hpp"
#include "deflux/runtime/research_harness.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace deflux::runtime {
namespace {

deflux::game::gta_sa_de::InProcessResearchController g_authoritative_research_controller{};

struct SyntheticPeConfig {
    std::uint32_t image_size = 0x8000;
    std::uint32_t text_rva = 0x1000;
    std::uint32_t rdata_rva = 0x4000;
    std::uint32_t anchor_rva = 0x5000;
    std::uint32_t lea_rva = 0x1100;
    std::uint32_t cmp_rva = 0x1150;
    std::uint32_t global_rva = 0x6000;
    std::uint8_t global_value = 9;
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
    constexpr char kAnchor[] = "menu_mainpresents";
    std::memcpy(image.data() + cfg.anchor_rva, kAnchor, sizeof(kAnchor));
    image[cfg.global_rva] = cfg.global_value;
    const std::int32_t lea_disp =
        static_cast<std::int32_t>(cfg.anchor_rva - (cfg.lea_rva + 7));
    image[cfg.lea_rva + 0] = 0x48;
    image[cfg.lea_rva + 1] = 0x8D;
    image[cfg.lea_rva + 2] = 0x05;
    std::memcpy(image.data() + cfg.lea_rva + 3, &lea_disp, sizeof(lea_disp));
    const std::int32_t cmp_disp =
        static_cast<std::int32_t>(cfg.global_rva - (cfg.cmp_rva + 7));
    image[cfg.cmp_rva + 0] = 0x80;
    image[cfg.cmp_rva + 1] = 0x3D;
    std::memcpy(image.data() + cfg.cmp_rva + 2, &cmp_disp, sizeof(cmp_disp));
    image[cfg.cmp_rva + 6] = 9;
    return image;
}

deflux::game::gta_sa_de::InProcessResearchConfig make_harness_config(
    std::uint32_t max_samples) {
    using namespace deflux::game::gta_sa_de;
    InProcessResearchConfig config{};
    config.research_enabled = true;
    config.experiment_id = kGGameStateInProcessExperimentId;
    config.experiment_version = kGGameStateInProcessExperimentVersion;
    config.capability = ResearchCapability::PassiveRead;
    config.expected_sha256 = kResearchAllowlistedSha256;
    config.interval_ms = kInProcessResearchMinIntervalMs;
    config.max_samples = max_samples;
    return config;
}

deflux::game::gta_sa_de::SameProcessModuleIdentity make_harness_identity(
    const std::vector<std::uint8_t>& pe) {
    using namespace deflux::game::gta_sa_de;
    SameProcessModuleIdentity identity{};
    identity.module_image = pe.data();
    identity.module_image_size = pe.size();
    identity.module_base = reinterpret_cast<std::uintptr_t>(pe.data());
    identity.executable_path = "C:\\Harness\\SanAndreas.exe";
    identity.sha256_hex = kResearchAllowlistedSha256;
    identity.skip_environment_preflight = true;
    return identity;
}

bool run_async_observing_shutdown_fixture() {
    using namespace deflux::game::gta_sa_de;
    using namespace deflux::game::gta_sa_de::detail;

    const std::uintptr_t identity_before = authoritative_research_controller_identity();
    auto& controller = g_authoritative_research_controller;
    std::string message;
    controller.shutdown(message);

    const SyntheticPeConfig pe_cfg{};
    const std::vector<std::uint8_t> pe = build_synthetic_pe(pe_cfg);
    if (!controller.configure(make_harness_config(120), message)) {
        return false;
    }
    if (!controller.arm(make_harness_identity(pe), message)) {
        return false;
    }
    if (!controller.begin_observation(message)) {
        return false;
    }
    if (!in_process_research_test_wait_until_observing(controller, 2000)) {
        controller.shutdown(message);
        return false;
    }
    if (controller.substate() != ResearchSubstate::Observing) {
        controller.shutdown(message);
        return false;
    }

    shutdown_research_from_host();

    if (authoritative_research_controller_identity() != identity_before) {
        return false;
    }
    const ResearchSubstate final_state = controller.substate();
    return final_state == ResearchSubstate::Disabled
        && !in_process_research_test_sampler_thread_joinable(controller);
}

bool run_auto_stop_fixture_once() {
    using namespace deflux::game::gta_sa_de;
    using namespace deflux::game::gta_sa_de::detail;

    auto& controller = g_authoritative_research_controller;
    std::string message;
    controller.shutdown(message);

    const SyntheticPeConfig pe_cfg{};
    const std::vector<std::uint8_t> pe = build_synthetic_pe(pe_cfg);
    if (!controller.configure(make_harness_config(3), message)) {
        return false;
    }
    if (!controller.arm(make_harness_identity(pe), message)) {
        return false;
    }
    if (!controller.begin_observation(message)) {
        return false;
    }
    if (!in_process_research_test_wait_until_substate(controller, ResearchSubstate::Stopped, 3000)) {
        controller.shutdown(message);
        return false;
    }
    controller.shutdown(message);
    return !in_process_research_test_sampler_thread_joinable(controller)
        && controller.substate() == ResearchSubstate::Disabled;
}

bool run_auto_stop_stress_fixture(std::uint32_t iterations) {
    for (std::uint32_t i = 0; i < iterations; ++i) {
        if (!run_auto_stop_fixture_once()) {
            return false;
        }
    }
    return true;
}

bool run_async_read_failure_shutdown_fixture() {
    using namespace deflux::game::gta_sa_de;
    using namespace deflux::game::gta_sa_de::detail;

    auto& controller = g_authoritative_research_controller;
    std::string message;
    controller.shutdown(message);

    const SyntheticPeConfig pe_cfg{};
    const std::vector<std::uint8_t> pe = build_synthetic_pe(pe_cfg);
    if (!controller.configure(make_harness_config(120), message)) {
        return false;
    }
    if (!controller.arm(make_harness_identity(pe), message)) {
        return false;
    }
    if (!controller.begin_observation(message)) {
        return false;
    }
    if (!in_process_research_test_wait_until_observing(controller, 2000)) {
        controller.shutdown(message);
        return false;
    }
    (void)in_process_research_test_force_derived_address(controller, 0x1);
    if (!in_process_research_test_wait_until_substate(controller, ResearchSubstate::Failed, 3000)) {
        controller.shutdown(message);
        return false;
    }
    shutdown_research_from_host();
    return controller.substate() == ResearchSubstate::Failed
        && !in_process_research_test_sampler_thread_joinable(controller);
}

bool run_async_shutdown_stress_fixture(std::uint32_t iterations) {
    for (std::uint32_t i = 0; i < iterations; ++i) {
        if (!run_async_observing_shutdown_fixture()) {
            return false;
        }
    }
    return true;
}

bool run_async_failure_stress_fixture(std::uint32_t iterations) {
    for (std::uint32_t i = 0; i < iterations; ++i) {
        if (!run_async_read_failure_shutdown_fixture()) {
            return false;
        }
    }
    return true;
}

} // namespace

deflux::game::gta_sa_de::InProcessResearchController& authoritative_research_controller()
    noexcept {
    return g_authoritative_research_controller;
}

void shutdown_research_from_host() noexcept {
    std::string message;
    (void)g_authoritative_research_controller.shutdown(message);
}

std::uintptr_t authoritative_research_controller_identity() noexcept {
    return reinterpret_cast<std::uintptr_t>(&g_authoritative_research_controller);
}

deflux::game::gta_sa_de::ResearchSubstate authoritative_research_substate() noexcept {
    return g_authoritative_research_controller.substate();
}

} // namespace deflux::runtime

extern "C" {

int32_t deflux_runtime_research_substate(void) {
    return static_cast<int32_t>(deflux::runtime::authoritative_research_substate());
}

uintptr_t deflux_runtime_research_controller_identity(void) {
    return deflux::runtime::authoritative_research_controller_identity();
}

DefluxRuntimeResult deflux_runtime_research_harness_async_shutdown_test(void) {
    return deflux::runtime::run_async_observing_shutdown_fixture() ? DEFLUX_RUNTIME_OK
                                                                   : DEFLUX_RUNTIME_INITIALIZATION_FAILED;
}

DefluxRuntimeResult deflux_runtime_research_harness_async_failure_shutdown_test(void) {
    return deflux::runtime::run_async_read_failure_shutdown_fixture() ? DEFLUX_RUNTIME_OK
                                                                      : DEFLUX_RUNTIME_INITIALIZATION_FAILED;
}

int32_t deflux_runtime_research_bootstrap_complete(void) {
    return deflux::runtime::research_bootstrap_complete() ? 1 : 0;
}

DefluxRuntimeResult deflux_runtime_research_harness_auto_stop_stress_test(void) {
    return deflux::runtime::run_auto_stop_stress_fixture(100) ? DEFLUX_RUNTIME_OK
                                                              : DEFLUX_RUNTIME_INITIALIZATION_FAILED;
}

DefluxRuntimeResult deflux_runtime_research_harness_async_shutdown_stress_test(void) {
    return deflux::runtime::run_async_shutdown_stress_fixture(100) ? DEFLUX_RUNTIME_OK
                                                                   : DEFLUX_RUNTIME_INITIALIZATION_FAILED;
}

DefluxRuntimeResult deflux_runtime_research_harness_async_failure_stress_test(void) {
    return deflux::runtime::run_async_failure_stress_fixture(100) ? DEFLUX_RUNTIME_OK
                                                                  : DEFLUX_RUNTIME_INITIALIZATION_FAILED;
}

DefluxRuntimeResult deflux_runtime_research_harness_lifecycle_breadcrumb_smoke(void) {
    using deflux::game::gta_sa_de::research_evidence_append_lifecycle;
    using deflux::game::gta_sa_de::research_evidence_default_directory_utf8;
    research_evidence_append_lifecycle("HARNESS_BREADCRUMB_SMOKE", "ok");
    const std::string dir = research_evidence_default_directory_utf8();
    if (dir.empty()) {
        return DEFLUX_RUNTIME_INITIALIZATION_FAILED;
    }
    std::string path = dir;
    if (path.back() != '\\' && path.back() != '/') {
        path.push_back('\\');
    }
    char file_name[64]{};
    std::snprintf(file_name,
                  sizeof(file_name),
                  "lifecycle_%lu.log",
                  static_cast<unsigned long>(GetCurrentProcessId()));
    path += file_name;
    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, path.c_str(), "r") != 0 || file == nullptr) {
        return DEFLUX_RUNTIME_INITIALIZATION_FAILED;
    }
#else
    file = std::fopen(path.c_str(), "r");
    if (file == nullptr) {
        return DEFLUX_RUNTIME_INITIALIZATION_FAILED;
    }
#endif
    char line[512]{};
    bool found = false;
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        if (std::strstr(line, "HARNESS_BREADCRUMB_SMOKE") != nullptr) {
            found = true;
            break;
        }
    }
    std::fclose(file);
    return found ? DEFLUX_RUNTIME_OK : DEFLUX_RUNTIME_INITIALIZATION_FAILED;
}

} // extern "C"
