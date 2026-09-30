#include "research_activation.hpp"

#include "in_process_research_host.hpp"

#include "deflux/game/gta_sa_de/in_process_research.hpp"
#include "deflux/game/gta_sa_de/research_build_allowlist.hpp"
#include "deflux/game/gta_sa_de/behavioral_transform_research.hpp"
#include "deflux/game/gta_sa_de/research_activation_status.hpp"
#include "deflux/game/gta_sa_de/research_evidence_log.hpp"
#include "deflux/game/gta_sa_de/research_intent.hpp"
#include "deflux/runtime/abi.h"
#include "discovery.hpp"
#include "environment_preflight.hpp"
#include "fingerprint.hpp"
#include "process_query.hpp"
#include "sha256.hpp"
#include "win_utf8.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <chrono>
#include <cstring>
#include <exception>
#include <vector>

namespace deflux::runtime {
namespace {

std::uint64_t steady_ms() {
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch())
            .count());
}

bool query_current_process_identity(deflux::core::ProcessInstance& out, std::string& error) {
    out = {};
    error.clear();
    const DWORD pid = GetCurrentProcessId();
    std::wstring image;
    std::uint64_t creation = 0;
    if (!deflux::game::gta_sa_de::detail::query_process_identity(
            static_cast<std::uint32_t>(pid), image, creation)) {
        error = "Could not query current process identity.";
        return false;
    }
    out.process_id = static_cast<std::uint32_t>(pid);
    out.creation_identity = creation;
    out.executable_path = deflux::game::gta_sa_de::detail::wide_to_utf8(image);
    if (!deflux::core::is_valid_process_instance(out)) {
        error = "Current process identity is incomplete.";
        return false;
    }
    return true;
}

bool is_fixture_executable_path(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    const std::wstring_view file =
        slash == std::wstring::npos ? std::wstring_view(path) : std::wstring_view(path).substr(slash + 1);
    return deflux::game::gta_sa_de::detail::equals_ignore_case(file,
                                                              L"DeFlux_ResearchLoader_Fixture.exe");
}

bool is_gta_research_host_path(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    const auto slash = path.find_last_of(L"\\/");
    const std::wstring_view file =
        slash == std::wstring::npos ? std::wstring_view(path) : std::wstring_view(path).substr(slash + 1);
    if (!deflux::game::gta_sa_de::detail::equals_ignore_case(file, L"SanAndreas.exe")) {
        return false;
    }
    return deflux::game::gta_sa_de::detail::looks_like_gta_sa_de_layout(path);
}

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

std::vector<std::uint8_t> g_synthetic_pe_storage;

bool build_live_module_identity(deflux::game::gta_sa_de::SameProcessModuleIdentity& identity,
                                std::string& error) {
    error.clear();
    const std::wstring exe_wide =
        deflux::game::gta_sa_de::detail::utf8_to_wide(identity.executable_path);
    const auto fp = deflux::game::gta_sa_de::detail::fingerprint_executable(exe_wide);
    if (!fp.ok || fp.sha256_hex.size() != 64) {
        error = "Failed to fingerprint host executable.";
        return false;
    }
    identity.sha256_hex = fp.sha256_hex;

    HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        error = "GetModuleHandleW(nullptr) failed.";
        return false;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos == nullptr || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        error = "Host module DOS header is invalid.";
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const std::uint8_t*>(module) + dos->e_lfanew);
    if (nt == nullptr || nt->Signature != IMAGE_NT_SIGNATURE) {
        error = "Host module NT header is invalid.";
        return false;
    }
    identity.module_base = reinterpret_cast<std::uintptr_t>(module);
    identity.module_image = reinterpret_cast<const std::uint8_t*>(module);
    identity.module_image_size = nt->OptionalHeader.SizeOfImage;
    identity.skip_environment_preflight = false;
    return true;
}

bool build_synthetic_module_identity(const deflux::core::ProcessInstance& current,
                                     deflux::game::gta_sa_de::SameProcessModuleIdentity& identity,
                                     std::string& error) {
    error.clear();
    g_synthetic_pe_storage = build_synthetic_pe(SyntheticPeConfig{});
    identity.module_image = g_synthetic_pe_storage.data();
    identity.module_image_size = g_synthetic_pe_storage.size();
    identity.module_base = reinterpret_cast<std::uintptr_t>(g_synthetic_pe_storage.data());
    identity.executable_path = current.executable_path;
    identity.sha256_hex = deflux::game::gta_sa_de::kResearchAllowlistedSha256;
    identity.skip_environment_preflight = true;
    return true;
}

void reject_intent(const std::string& reason) {
    std::string message;
    (void)deflux::game::gta_sa_de::update_research_intent_mapping_state(
        deflux::game::gta_sa_de::ResearchIntentState::Pending,
        deflux::game::gta_sa_de::ResearchIntentState::Rejected,
        message);
    (void)reason;
}

std::string read_evidence_directory_env_utf8() {
    wchar_t buffer[1024]{};
    const DWORD length = GetEnvironmentVariableW(L"DEFLUX_RESEARCH_EVIDENCE_DIR", buffer, 1024);
    if (length == 0 || length >= 1024) {
        return {};
    }
    return deflux::game::gta_sa_de::detail::wide_to_utf8(buffer);
}

void publish_runtime_snapshot(std::uint32_t intent_state,
                              deflux::game::gta_sa_de::ResearchSubstate substate,
                              std::uint32_t samples,
                              bool observation_complete) {
    deflux::game::gta_sa_de::research_activation_status_publish_controller(
        intent_state,
        static_cast<std::uint32_t>(substate),
        samples,
        observation_complete);
}

bool consume_intent() {
    std::string message;
    return deflux::game::gta_sa_de::update_research_intent_mapping_state(
        deflux::game::gta_sa_de::ResearchIntentState::Pending,
        deflux::game::gta_sa_de::ResearchIntentState::Consumed,
        message);
}

bool synthetic_async_sampler_requested() noexcept {
    wchar_t buffer[8]{};
    const DWORD length = GetEnvironmentVariableW(L"DEFLUX_RESEARCH_SYNTHETIC_ASYNC", buffer, 8);
    if (length == 0 || length >= 8) {
        return false;
    }
    return buffer[0] == L'1' || buffer[0] == L'Y' || buffer[0] == L'y'
        || buffer[0] == L'T' || buffer[0] == L't';
}

} // namespace

void try_activate_research_from_intent() noexcept {
    using namespace deflux::game::gta_sa_de;

    try {
    deflux::game::gta_sa_de::ResearchIntentRecord record{};
    bool found = false;
    std::string message;
    if (!try_open_research_intent_record(record, found, message)) {
        return;
    }
    if (!found) {
        publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Empty),
                                 ResearchSubstate::Disabled,
                                 0,
                                 false);
        return;
    }

    const bool synthetic = record.magic == kResearchIntentMagicSynthetic;
    const bool live = record.magic == kResearchIntentMagicLive;
    if (!live && !synthetic) {
        reject_intent("Intent magic is not authorized.");
        publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Rejected),
                                 ResearchSubstate::Disabled,
                                 0,
                                 false);
        return;
    }

    if (is_research_intent_stale(record, steady_ms(), message)) {
        reject_intent(message);
        publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Rejected),
                                 ResearchSubstate::Disabled,
                                 0,
                                 false);
        return;
    }

    if (!validate_research_intent_record(record, synthetic, message)) {
        reject_intent(message);
        publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Rejected),
                                 ResearchSubstate::Disabled,
                                 0,
                                 false);
        return;
    }
    research_evidence_append_lifecycle("INTENT_VALIDATED", synthetic ? "synthetic" : "live");

    deflux::core::ProcessInstance current{};
    if (!query_current_process_identity(current, message)) {
        reject_intent(message);
        publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Rejected),
                                 ResearchSubstate::Disabled,
                                 0,
                                 false);
        return;
    }

    if (!research_intent_matches_process(record,
                                         current.process_id,
                                         current.creation_identity,
                                         current.executable_path.c_str())) {
        reject_intent("Intent target identity does not match current process.");
        publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Rejected),
                                 ResearchSubstate::Disabled,
                                 0,
                                 false);
        return;
    }

    const std::wstring exe_wide = detail::utf8_to_wide(current.executable_path);
    if (live) {
        if (!is_gta_research_host_path(exe_wide)) {
            reject_intent("Host executable is not the expected GTA SA DE research target.");
            publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Rejected),
                                     ResearchSubstate::Disabled,
                                     0,
                                     false);
            return;
        }
        if (std::strcmp(record.expected_sha256, kResearchAllowlistedSha256) != 0) {
            reject_intent("Intent SHA-256 does not match research allowlist entry.");
            publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Rejected),
                                     ResearchSubstate::Disabled,
                                     0,
                                     false);
            return;
        }
        const EnvironmentPreflightResult preflight =
            detail::scan_game_executable_directory(current.executable_path);
        if (!preflight.clean) {
            reject_intent(preflight.message.empty() ? "Environment preflight failed."
                                                    : preflight.message);
            publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Rejected),
                                     ResearchSubstate::Disabled,
                                     0,
                                     false);
            return;
        }
    } else if (!is_fixture_executable_path(exe_wide)) {
        reject_intent("Synthetic intent requires the research loader fixture host.");
        publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Rejected),
                                 ResearchSubstate::Disabled,
                                 0,
                                 false);
        return;
    }

    if (std::strcmp(record.experiment_id, kBehavioralTransformExperimentId) == 0) {
        if (!live) {
            reject_intent("Behavioral transform research requires the live research host.");
            publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Rejected),
                                     ResearchSubstate::Disabled,
                                     0,
                                     false);
            return;
        }
        SameProcessModuleIdentity identity{};
        identity.executable_path = current.executable_path;
        if (!build_live_module_identity(identity, message)
            || identity.sha256_hex != record.expected_sha256) {
            reject_intent("Observed host SHA-256 does not match the behavioral session.");
            publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Rejected),
                                     ResearchSubstate::Disabled,
                                     0,
                                     false);
            return;
        }
        if (!consume_intent()) {
            publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Rejected),
                                     ResearchSubstate::Disabled,
                                     0,
                                     false);
            return;
        }
        run_behavioral_transform_session(record.session_nonce,
                                         current.process_id,
                                         current.creation_identity,
                                         record.expected_sha256,
                                         identity.sha256_hex.c_str());
        return;
    }

    InProcessResearchConfig config{};
    config.research_enabled = true;
    config.experiment_id = kGGameStateInProcessExperimentId;
    config.experiment_version = kGGameStateInProcessExperimentVersion;
    config.capability = ResearchCapability::PassiveRead;
    config.expected_sha256 = record.expected_sha256;
    config.interval_ms = record.interval_ms;
    config.max_samples = record.max_samples;

    auto& controller = authoritative_research_controller();
    if (!controller.configure(config, message)) {
        reject_intent(message);
        publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Rejected),
                                 ResearchSubstate::Disabled,
                                 0,
                                 false);
        return;
    }
    research_evidence_append_lifecycle("CONTROLLER_CONFIGURED", "");

    SameProcessModuleIdentity identity{};
    identity.executable_path = current.executable_path;
    if (live) {
        if (!build_live_module_identity(identity, message)) {
            reject_intent(message);
            publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Rejected),
                                     ResearchSubstate::Disabled,
                                     0,
                                     false);
            return;
        }
        if (identity.sha256_hex != record.expected_sha256) {
            reject_intent("Observed host SHA-256 does not match intent.");
            publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Rejected),
                                     ResearchSubstate::Disabled,
                                     0,
                                     false);
            return;
        }
    } else if (!build_synthetic_module_identity(current, identity, message)) {
        reject_intent(message);
        publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Rejected),
                                 ResearchSubstate::Disabled,
                                 0,
                                 false);
        return;
    }

    if (!controller.arm(identity, message)) {
        reject_intent(message);
        publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Rejected),
                                 ResearchSubstate::Disabled,
                                 0,
                                 false);
        return;
    }
    research_evidence_append_lifecycle("CONTROLLER_ARMED", "");

    if (!consume_intent()) {
        controller.shutdown(message);
        publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Rejected),
                                 ResearchSubstate::Disabled,
                                 0,
                                 false);
        return;
    }

    const std::string evidence_override = read_evidence_directory_env_utf8();
    if (!evidence_override.empty()) {
        research_evidence_set_output_directory_override(evidence_override);
    }

    research_activation_status_bind_session(synthetic, record.session_nonce, record.max_samples);
    research_evidence_set_hook_enabled(true);
    ResearchEvidenceSessionHeader header{};
    header.process_id = current.process_id;
    header.creation_identity = current.creation_identity;
    header.session_nonce = record.session_nonce;
    header.experiment_id = kGGameStateInProcessExperimentId;
    header.experiment_version = kGGameStateInProcessExperimentVersion;
    header.expected_sha256 = record.expected_sha256;
    header.observed_sha256 = identity.sha256_hex;
    header.pipeline_proof = synthetic;
    std::string log_path;
    if (!research_evidence_begin_session(header, log_path)) {
        controller.shutdown(message);
        publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Consumed),
                                 ResearchSubstate::Failed,
                                 0,
                                 true);
        return;
    }

    // Live GTA always uses async _beginthreadex. Synthetic default remains calling-thread
    // pipeline proof; DEFLUX_RESEARCH_SYNTHETIC_ASYNC=1 selects the live-equivalent async path.
    const bool use_async_sampler = live || (synthetic && synthetic_async_sampler_requested());

    if (!use_async_sampler) {
        research_activation_status_publish_controller(
            static_cast<std::uint32_t>(ResearchIntentState::Consumed),
            static_cast<std::uint32_t>(ResearchSubstate::Observing),
            0,
            false);
        if (!controller.begin_observation_on_calling_thread(message)) {
            research_evidence_end_session(ResearchSubstate::Failed,
                                          InProcessSignalClassification::None,
                                          message);
            controller.shutdown(message);
            publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Consumed),
                                     ResearchSubstate::Failed,
                                     0,
                                     true);
            return;
        }
        const auto status = controller.status_snapshot();
        publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Consumed),
                                 status.substate,
                                 static_cast<std::uint32_t>(status.samples_collected),
                                 status.substate == ResearchSubstate::Stopped
                                     || status.substate == ResearchSubstate::Failed);
        return;
    }

    research_evidence_append_lifecycle("SAMPLER_THREAD_CREATE_BEGIN", live ? "live" : "synthetic_async");
    if (!controller.begin_observation(message)) {
        research_evidence_append_lifecycle("SAMPLER_THREAD_CREATE_FAILED", message.c_str());
        research_evidence_end_session(ResearchSubstate::Failed,
                                      InProcessSignalClassification::None,
                                      message);
        controller.shutdown(message);
        publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Consumed),
                                 ResearchSubstate::Failed,
                                 0,
                                 true);
        return;
    }
    research_evidence_append_lifecycle("SAMPLER_THREAD_CREATE_OK", "");

    publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Consumed),
                             ResearchSubstate::Observing,
                             0,
                             false);

    // CRITICAL: do not let this CRT bootstrap worker return while the async sampler
    // is still live. Phase 2 / synthetic-async repro showed ucrtbased.dll AV
    // (c0000005) when BOOTSTRAP_THREAD_EXIT interleaved with SAMPLE_* on the
    // sampler thread after remote LoadLibrary into a foreign process.
    const std::uint64_t wait_budget_ms =
        static_cast<std::uint64_t>(config.interval_ms) * static_cast<std::uint64_t>(config.max_samples)
        + 5000ull;
    const std::uint64_t deadline = steady_ms() + wait_budget_ms;
    while (steady_ms() < deadline) {
        const ResearchSubstate state = controller.substate();
        if (state == ResearchSubstate::Stopped || state == ResearchSubstate::Failed) {
            break;
        }
        Sleep(10);
    }
    {
        std::string join_message;
        (void)controller.stop_observation(join_message);
    }
    const auto final_status = controller.status_snapshot();
    research_evidence_append_lifecycle("ASYNC_BOOTSTRAP_WAIT_DONE",
                                       to_string(final_status.substate));
    publish_runtime_snapshot(static_cast<std::uint32_t>(ResearchIntentState::Consumed),
                             final_status.substate,
                             static_cast<std::uint32_t>(final_status.samples_collected),
                             final_status.substate == ResearchSubstate::Stopped
                                 || final_status.substate == ResearchSubstate::Failed);
    } catch (const std::exception& ex) {
        research_evidence_append_lifecycle("ACTIVATION_EXCEPTION", ex.what());
    } catch (...) {
        research_evidence_append_lifecycle("ACTIVATION_EXCEPTION", "unknown");
    }
}

} // namespace deflux::runtime
