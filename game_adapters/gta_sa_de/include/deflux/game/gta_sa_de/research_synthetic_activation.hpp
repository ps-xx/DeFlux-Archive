#pragma once

// Task #9B Gate 2B Phase 1A: synthetic end-to-end activation orchestration (fixture only).

#include "deflux/game/gta_sa_de/research_intent.hpp"
#include "deflux/game/gta_sa_de/research_remote_loader.hpp"

#include <cstdint>
#include <string>

namespace deflux::game::gta_sa_de {

struct SyntheticActivationE2EResult {
    bool ok = false;
    ResearchLoadResult load_result = ResearchLoadResult::LoadNotRequested;
    bool intent_consumed = false;
    bool bootstrap_complete = false;
    std::uint32_t final_substate = 0;
    std::uint32_t samples_collected = 0;
    bool observation_complete = false;
    bool pipeline_proof = false;
    std::string evidence_log_path;
    std::string message;
};

struct SyntheticActivationE2EOptions {
    std::wstring fixture_exe_path;
    std::wstring launcher_module_path;
    std::wstring runtime_dll_path;
    std::string evidence_directory_utf8;
    deflux::core::ProcessInstance target{};
    std::uint32_t interval_ms = 50;
    std::uint32_t max_samples = 5;
    std::uint64_t session_nonce = 0xE2E001u;
    bool publish_intent = true;
    bool expect_activation = true;
    // When true, fixture child must run with DEFLUX_RESEARCH_SYNTHETIC_ASYNC=1 so the
    // remote-loaded runtime uses begin_observation() (live-equivalent async path).
    bool use_async_sampler = false;
    ResearchIntentPublishRequest intent_override{};
    bool use_intent_override = false;
    std::uint32_t wait_timeout_ms = 15000;
};

SyntheticActivationE2EResult execute_synthetic_activation_e2e(
    const SyntheticActivationE2EOptions& options) noexcept;

std::string find_latest_evidence_log_in_directory(const std::string& directory_utf8,
                                                  std::uint64_t session_nonce) noexcept;

bool evidence_log_contains_required_fields(const std::string& log_path,
                                           std::uint32_t max_samples,
                                           bool expect_pipeline_proof) noexcept;

void delete_file_if_exists_utf8(const std::string& path_utf8) noexcept;

} // namespace deflux::game::gta_sa_de
