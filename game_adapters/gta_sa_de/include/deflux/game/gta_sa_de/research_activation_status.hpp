#pragma once

// Task #9B Gate 2B Phase 1A: cross-process activation status (synthetic tests only).

#include <cstdint>

namespace deflux::game::gta_sa_de {

inline constexpr std::uint32_t kResearchActivationStatusMagic = 0x53544241u; // 'ABTS'
inline constexpr std::uint32_t kResearchActivationStatusSchemaVersion = 1;

#pragma pack(push, 1)
struct ResearchActivationStatusRecord {
    std::uint32_t magic = 0;
    std::uint32_t schema_version = 0;
    std::uint32_t bootstrap_complete = 0;
    std::uint32_t intent_state = 0;
    std::uint32_t research_substate = 0;
    std::uint32_t samples_collected = 0;
    std::uint32_t observation_complete = 0;
    std::uint32_t pipeline_proof = 0;
    std::uint64_t session_nonce = 0;
    std::uint32_t configured_max_samples = 0;
};
#pragma pack(pop)

static_assert(sizeof(ResearchActivationStatusRecord) <= 64,
              "ResearchActivationStatusRecord must stay compact.");

void research_activation_status_reset_for_tests(std::uint64_t session_nonce = 0) noexcept;

bool research_activation_status_try_read(ResearchActivationStatusRecord& out,
                                         bool& found,
                                         std::uint64_t session_nonce = 0) noexcept;

void research_activation_status_publish_bootstrap_complete() noexcept;

void research_activation_status_publish_runtime_state(std::uint32_t intent_state,
                                                      std::uint32_t research_substate,
                                                      std::uint32_t samples_collected,
                                                      bool observation_complete,
                                                      bool pipeline_proof,
                                                      std::uint64_t session_nonce) noexcept;

bool research_activation_status_wait_for(std::uint32_t expected_research_substate,
                                         std::uint32_t min_samples,
                                         bool expect_observation_complete,
                                         std::uint32_t timeout_ms,
                                         std::uint64_t session_nonce,
                                         ResearchActivationStatusRecord& out) noexcept;

void research_activation_status_bind_session(bool pipeline_proof,
                                             std::uint64_t session_nonce,
                                             std::uint32_t configured_max_samples) noexcept;

void research_activation_status_publish_controller(std::uint32_t intent_state,
                                                   std::uint32_t research_substate,
                                                   std::uint32_t samples_collected,
                                                   bool observation_complete) noexcept;

} // namespace deflux::game::gta_sa_de
