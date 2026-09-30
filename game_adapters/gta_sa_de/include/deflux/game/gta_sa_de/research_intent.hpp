#pragma once

// Task #9B Gate 2B: per-session research intent (Local file mapping, not named pipe).

#include "deflux/core/process_lifecycle.hpp"

#include <cstdint>
#include <string>

namespace deflux::game::gta_sa_de {

inline constexpr std::uint32_t kResearchIntentSchemaVersion = 1;
inline constexpr std::uint32_t kResearchIntentMagicLive = 0x42394644u; // 'DF9B'
inline constexpr std::uint32_t kResearchIntentMagicSynthetic = 0x53394644u; // 'DF9S'
inline constexpr std::size_t kResearchIntentExperimentIdCapacity = 48;
inline constexpr std::size_t kResearchIntentShaCapacity = 65;
inline constexpr std::uint32_t kResearchIntentMaxAgeMs = 120000;

enum class ResearchIntentState : std::uint32_t {
    Empty = 0,
    Pending = 1,
    Consumed = 2,
    Rejected = 3,
};

#pragma pack(push, 1)
struct ResearchIntentRecord {
    std::uint32_t magic = 0;
    std::uint32_t schema_version = 0;
    std::uint32_t state = 0;
    std::uint32_t target_process_id = 0;
    std::uint64_t target_creation_identity = 0;
    std::uint64_t session_nonce = 0;
    std::uint32_t experiment_version = 0;
    std::uint32_t capability = 0;
    std::uint32_t interval_ms = 0;
    std::uint32_t max_samples = 0;
    char experiment_id[kResearchIntentExperimentIdCapacity]{};
    char expected_sha256[kResearchIntentShaCapacity]{};
    std::uint64_t published_at_ms = 0;
};
#pragma pack(pop)

static_assert(sizeof(ResearchIntentRecord) <= 256, "ResearchIntentRecord must stay compact.");

struct ResearchIntentPublishRequest {
    deflux::core::ProcessInstance target{};
    std::uint64_t session_nonce = 0;
    std::uint32_t interval_ms = 500;
    std::uint32_t max_samples = 120;
    const char* expected_sha256 = nullptr;
    const char* experiment_id = nullptr;
    bool synthetic_fixture = false;
};

struct ResearchIntentSession {
    void* mapping = nullptr;
    ResearchIntentRecord* view = nullptr;
};

bool normalize_research_intent_publish_request(ResearchIntentPublishRequest& request,
                                               std::string& rejection_reason) noexcept;

bool validate_research_intent_record(const ResearchIntentRecord& record,
                                     bool synthetic_allowed,
                                     std::string& rejection_reason) noexcept;

bool research_intent_matches_process(const ResearchIntentRecord& record,
                                     std::uint32_t process_id,
                                     std::uint64_t creation_identity,
                                     const char* executable_path_utf8) noexcept;

bool publish_research_intent_session(ResearchIntentSession& session,
                                     const ResearchIntentPublishRequest& request,
                                     std::string& message_out) noexcept;

void close_research_intent_session(ResearchIntentSession& session) noexcept;

bool try_open_research_intent_record(ResearchIntentRecord& out_record,
                                     bool& found,
                                     std::string& message_out) noexcept;

bool mark_research_intent_consumed(std::string& message_out) noexcept;

bool mark_research_intent_rejected(std::string& message_out) noexcept;

bool wait_for_research_intent_consumed(std::uint32_t timeout_ms,
                                       std::string& message_out) noexcept;

bool is_research_intent_stale(const ResearchIntentRecord& record,
                              std::uint64_t now_ms,
                              std::string& rejection_reason) noexcept;

bool update_research_intent_mapping_state(ResearchIntentState required_current,
                                          ResearchIntentState desired,
                                          std::string& message_out) noexcept;

void reset_research_intent_mapping_for_tests() noexcept;

} // namespace deflux::game::gta_sa_de
