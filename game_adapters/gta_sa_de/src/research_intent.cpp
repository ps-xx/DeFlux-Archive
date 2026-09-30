#include "deflux/game/gta_sa_de/research_intent.hpp"

#include "deflux/game/gta_sa_de/in_process_research.hpp"
#include "deflux/game/gta_sa_de/research_build_allowlist.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <chrono>
#include <cstring>

namespace deflux::game::gta_sa_de {
namespace {

constexpr wchar_t kResearchIntentMappingName[] = L"Local\\DeFlux_9B_GGameState_ResearchIntent_v1";

std::uint64_t steady_ms() {
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch())
            .count());
}

ResearchIntentSession* g_active_session = nullptr;

bool copy_field(char* dest, std::size_t capacity, const char* src) {
    if (src == nullptr || capacity == 0) {
        return false;
    }
    std::memset(dest, 0, capacity);
    const std::size_t length = std::strlen(src);
    const std::size_t copy = length < (capacity - 1) ? length : (capacity - 1);
    if (copy > 0) {
        std::memcpy(dest, src, copy);
    }
    return dest[0] != '\0';
}

} // namespace

bool normalize_research_intent_publish_request(ResearchIntentPublishRequest& request,
                                               std::string& rejection_reason) noexcept {
    rejection_reason.clear();
    if (!deflux::core::is_valid_process_instance(request.target)) {
        rejection_reason = "Intent target process identity is invalid.";
        return false;
    }
    if (request.expected_sha256 == nullptr || request.expected_sha256[0] == '\0') {
        request.expected_sha256 = kResearchAllowlistedSha256;
    }
    if (!is_research_allowlisted_sha256(request.expected_sha256)) {
        rejection_reason = "Intent expected SHA-256 is not research-allowlisted.";
        return false;
    }
    if (request.interval_ms == 0) {
        request.interval_ms = kInProcessResearchDefaultIntervalMs;
    }
    if (request.max_samples == 0) {
        request.max_samples = kInProcessResearchDefaultMaxSamples;
    }
    if (request.interval_ms < kInProcessResearchMinIntervalMs) {
        rejection_reason = "Intent interval is below minimum bound.";
        return false;
    }
    if (request.interval_ms > kInProcessResearchMaxIntervalMs) {
        request.interval_ms = kInProcessResearchMaxIntervalMs;
    }
    if (request.max_samples > kInProcessResearchMaxSamplesCap) {
        request.max_samples = kInProcessResearchMaxSamplesCap;
    }
    if (request.session_nonce == 0) {
        request.session_nonce = steady_ms() ^ static_cast<std::uint64_t>(request.target.process_id);
    }
    return true;
}

bool validate_research_intent_record(const ResearchIntentRecord& record,
                                     bool synthetic_allowed,
                                     std::string& rejection_reason) noexcept {
    rejection_reason.clear();
    if (record.schema_version != kResearchIntentSchemaVersion) {
        rejection_reason = "Intent schema version mismatch.";
        return false;
    }
    const bool live = record.magic == kResearchIntentMagicLive;
    const bool synthetic = record.magic == kResearchIntentMagicSynthetic;
    if (!live && !(synthetic && synthetic_allowed)) {
        rejection_reason = "Intent magic is not authorized.";
        return false;
    }
    if (record.state != static_cast<std::uint32_t>(ResearchIntentState::Pending)) {
        rejection_reason = "Intent is not pending.";
        return false;
    }
    if (record.capability != static_cast<std::uint32_t>(ResearchCapability::PassiveRead)) {
        rejection_reason = "Intent capability is not PASSIVE_READ.";
        return false;
    }
    if (record.experiment_version != kGGameStateInProcessExperimentVersion) {
        rejection_reason = "Intent experiment version mismatch.";
        return false;
    }
    if (!is_supported_in_process_experiment_id(record.experiment_id)) {
        rejection_reason = "Intent experiment ID is not supported.";
        return false;
    }
    if (!is_research_allowlisted_sha256(record.expected_sha256)) {
        rejection_reason = "Intent SHA-256 is not research-allowlisted.";
        return false;
    }
    if (record.interval_ms < kInProcessResearchMinIntervalMs
        || record.interval_ms > kInProcessResearchMaxIntervalMs) {
        rejection_reason = "Intent interval is out of bounds.";
        return false;
    }
    if (record.max_samples == 0 || record.max_samples > kInProcessResearchMaxSamplesCap) {
        rejection_reason = "Intent max_samples is out of bounds.";
        return false;
    }
    if (record.target_process_id == 0 || record.target_creation_identity == 0) {
        rejection_reason = "Intent target identity is incomplete.";
        return false;
    }
    return true;
}

bool research_intent_matches_process(const ResearchIntentRecord& record,
                                     std::uint32_t process_id,
                                     std::uint64_t creation_identity,
                                     const char* executable_path_utf8) noexcept {
    if (record.target_process_id != process_id
        || record.target_creation_identity != creation_identity) {
        return false;
    }
    if (executable_path_utf8 == nullptr || executable_path_utf8[0] == '\0') {
        return false;
    }
    deflux::core::ProcessInstance expected{};
    expected.process_id = record.target_process_id;
    expected.creation_identity = record.target_creation_identity;
    expected.executable_path = executable_path_utf8;
    deflux::core::ProcessInstance actual{};
    actual.process_id = process_id;
    actual.creation_identity = creation_identity;
    actual.executable_path = executable_path_utf8;
    return deflux::core::same_process_instance(expected, actual);
}

bool publish_research_intent_session(ResearchIntentSession& session,
                                     const ResearchIntentPublishRequest& request,
                                     std::string& message_out) noexcept {
    message_out.clear();
    close_research_intent_session(session);
    if (HANDLE stale = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kResearchIntentMappingName);
        stale != nullptr) {
        CloseHandle(stale);
    }

    ResearchIntentPublishRequest normalized = request;
    if (!normalize_research_intent_publish_request(normalized, message_out)) {
        return false;
    }

    session.mapping =
        CreateFileMappingW(INVALID_HANDLE_VALUE,
                           nullptr,
                           PAGE_READWRITE,
                           0,
                           static_cast<DWORD>(sizeof(ResearchIntentRecord)),
                           kResearchIntentMappingName);
    if (session.mapping == nullptr) {
        message_out = "CreateFileMappingW for research intent failed.";
        return false;
    }
    session.view = static_cast<ResearchIntentRecord*>(
        MapViewOfFile(session.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ResearchIntentRecord)));
    if (session.view == nullptr) {
        message_out = "MapViewOfFile for research intent failed.";
        close_research_intent_session(session);
        return false;
    }

    ResearchIntentRecord record{};
    record.magic = normalized.synthetic_fixture ? kResearchIntentMagicSynthetic
                                                : kResearchIntentMagicLive;
    record.schema_version = kResearchIntentSchemaVersion;
    record.state = static_cast<std::uint32_t>(ResearchIntentState::Pending);
    record.target_process_id = normalized.target.process_id;
    record.target_creation_identity = normalized.target.creation_identity;
    record.session_nonce = normalized.session_nonce;
    record.experiment_version = kGGameStateInProcessExperimentVersion;
    record.capability = static_cast<std::uint32_t>(ResearchCapability::PassiveRead);
    record.interval_ms = normalized.interval_ms;
    record.max_samples = normalized.max_samples;
    record.published_at_ms = steady_ms();
    const char* experiment_id = normalized.experiment_id != nullptr
        ? normalized.experiment_id
        : kGGameStateInProcessExperimentId;
    if (!copy_field(record.experiment_id,
                    kResearchIntentExperimentIdCapacity,
                    experiment_id)
        || !copy_field(record.expected_sha256,
                       kResearchIntentShaCapacity,
                       normalized.expected_sha256)) {
        message_out = "Intent field copy failed.";
        close_research_intent_session(session);
        return false;
    }
    *session.view = record;
    g_active_session = &session;
    message_out = "Research intent published.";
    return true;
}

void close_research_intent_session(ResearchIntentSession& session) noexcept {
    if (session.view != nullptr) {
        UnmapViewOfFile(session.view);
        session.view = nullptr;
    }
    if (session.mapping != nullptr) {
        CloseHandle(static_cast<HANDLE>(session.mapping));
        session.mapping = nullptr;
    }
    if (g_active_session == &session) {
        g_active_session = nullptr;
    }
}

bool try_open_research_intent_record(ResearchIntentRecord& out_record,
                                     bool& found,
                                     std::string& message_out) noexcept {
    message_out.clear();
    found = false;
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, kResearchIntentMappingName);
    if (mapping == nullptr) {
        return true;
    }
    const auto* view = static_cast<const ResearchIntentRecord*>(
        MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(ResearchIntentRecord)));
    if (view == nullptr) {
        CloseHandle(mapping);
        message_out = "MapViewOfFile read for research intent failed.";
        return false;
    }
    out_record = *view;
    found = true;
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    return true;
}

bool mark_research_intent_consumed(std::string& message_out) noexcept {
    message_out.clear();
    if (g_active_session == nullptr || g_active_session->view == nullptr) {
        message_out = "No active research intent mapping to consume.";
        return false;
    }
    g_active_session->view->state = static_cast<std::uint32_t>(ResearchIntentState::Consumed);
    message_out = "Research intent marked consumed.";
    return true;
}

bool mark_research_intent_rejected(std::string& message_out) noexcept {
    message_out.clear();
    if (g_active_session == nullptr || g_active_session->view == nullptr) {
        message_out = "No active research intent mapping to reject.";
        return false;
    }
    g_active_session->view->state = static_cast<std::uint32_t>(ResearchIntentState::Rejected);
    message_out = "Research intent marked rejected.";
    return true;
}

bool is_research_intent_stale(const ResearchIntentRecord& record,
                              std::uint64_t now_ms,
                              std::string& rejection_reason) noexcept {
    rejection_reason.clear();
    if (record.published_at_ms == 0) {
        rejection_reason = "Intent publish timestamp is missing.";
        return true;
    }
    if (now_ms + 1000 < record.published_at_ms) {
        rejection_reason = "Intent publish timestamp is in the future.";
        return true;
    }
    if (now_ms - record.published_at_ms > kResearchIntentMaxAgeMs) {
        rejection_reason = "Intent is stale.";
        return true;
    }
    return false;
}

bool update_research_intent_mapping_state(ResearchIntentState required_current,
                                          ResearchIntentState desired,
                                          std::string& message_out) noexcept {
    message_out.clear();
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kResearchIntentMappingName);
    if (mapping == nullptr) {
        message_out = "Research intent mapping is not available.";
        return false;
    }
    auto* view = static_cast<ResearchIntentRecord*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ResearchIntentRecord)));
    if (view == nullptr) {
        CloseHandle(mapping);
        message_out = "MapViewOfFile write for research intent failed.";
        return false;
    }
    if (view->state != static_cast<std::uint32_t>(required_current)) {
        UnmapViewOfFile(view);
        CloseHandle(mapping);
        message_out = "Research intent state transition rejected.";
        return false;
    }
    view->state = static_cast<std::uint32_t>(desired);
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    if (g_active_session != nullptr && g_active_session->view != nullptr) {
        g_active_session->view->state = static_cast<std::uint32_t>(desired);
    }
    message_out = "Research intent state updated.";
    return true;
}

void reset_research_intent_mapping_for_tests() noexcept {
    g_active_session = nullptr;
    for (int attempt = 0; attempt < 8; ++attempt) {
        HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kResearchIntentMappingName);
        if (mapping == nullptr) {
            break;
        }
        auto* view = static_cast<ResearchIntentRecord*>(
            MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ResearchIntentRecord)));
        if (view != nullptr) {
            *view = ResearchIntentRecord{};
            UnmapViewOfFile(view);
        }
        CloseHandle(mapping);
    }
}

bool wait_for_research_intent_consumed(std::uint32_t timeout_ms, std::string& message_out) noexcept {
    message_out.clear();
    const std::uint64_t deadline = steady_ms() + timeout_ms;
    while (steady_ms() < deadline) {
        ResearchIntentRecord record{};
        bool found = false;
        if (!try_open_research_intent_record(record, found, message_out)) {
            return false;
        }
        if (found) {
            if (record.state == static_cast<std::uint32_t>(ResearchIntentState::Consumed)) {
                message_out = "Research intent consumed.";
                return true;
            }
            if (record.state == static_cast<std::uint32_t>(ResearchIntentState::Rejected)) {
                message_out = "Research intent rejected by runtime.";
                return false;
            }
        }
        Sleep(25);
    }
    message_out = "Timed out waiting for research intent consumption.";
    return false;
}

} // namespace deflux::game::gta_sa_de
