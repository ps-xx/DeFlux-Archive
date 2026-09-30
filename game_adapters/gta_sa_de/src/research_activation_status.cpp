#include "deflux/game/gta_sa_de/research_activation_status.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <chrono>
#include <cstring>

namespace deflux::game::gta_sa_de {
namespace {

constexpr wchar_t kResearchActivationStatusMappingName[] =
    L"Local\\DeFlux_9B_ResearchActivationStatus_v1";

void mapping_name_for(std::uint64_t nonce, wchar_t* out, std::size_t out_count) noexcept {
    if (nonce == 0) {
        wcsncpy_s(out, out_count, kResearchActivationStatusMappingName, _TRUNCATE);
        return;
    }
    swprintf_s(out,
               out_count,
               L"%s_%016llX",
               kResearchActivationStatusMappingName,
               static_cast<unsigned long long>(nonce));
}

ResearchActivationStatusRecord* g_status_view = nullptr;
HANDLE g_status_mapping = nullptr;
bool g_pipeline_proof = false;
std::uint64_t g_session_nonce = 0;
std::uint32_t g_configured_max_samples = 0;

std::uint64_t steady_ms() {
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch())
            .count());
}

bool ensure_status_mapping() {
    if (g_status_view != nullptr) {
        return true;
    }
    wchar_t mapping_name[96]{};
    mapping_name_for(g_session_nonce, mapping_name, 96);
    g_status_mapping =
        CreateFileMappingW(INVALID_HANDLE_VALUE,
                           nullptr,
                           PAGE_READWRITE,
                           0,
                           static_cast<DWORD>(sizeof(ResearchActivationStatusRecord)),
                           mapping_name);
    if (g_status_mapping == nullptr) {
        return false;
    }
    g_status_view = static_cast<ResearchActivationStatusRecord*>(MapViewOfFile(
        g_status_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ResearchActivationStatusRecord)));
    if (g_status_view == nullptr) {
        CloseHandle(g_status_mapping);
        g_status_mapping = nullptr;
        return false;
    }
    if (g_status_view->magic != kResearchActivationStatusMagic) {
        *g_status_view = ResearchActivationStatusRecord{};
        g_status_view->magic = kResearchActivationStatusMagic;
        g_status_view->schema_version = kResearchActivationStatusSchemaVersion;
    }
    return true;
}

void close_status_mapping() {
    if (g_status_view != nullptr) {
        UnmapViewOfFile(g_status_view);
        g_status_view = nullptr;
    }
    if (g_status_mapping != nullptr) {
        CloseHandle(g_status_mapping);
        g_status_mapping = nullptr;
    }
}

} // namespace

void zero_named_status(std::uint64_t session_nonce) noexcept {
    wchar_t mapping_name[96]{};
    mapping_name_for(session_nonce, mapping_name, 96);
    if (HANDLE existing = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mapping_name);
        existing != nullptr) {
        auto* view = static_cast<ResearchActivationStatusRecord*>(
            MapViewOfFile(existing, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ResearchActivationStatusRecord)));
        if (view != nullptr) {
            *view = ResearchActivationStatusRecord{};
            UnmapViewOfFile(view);
        }
        CloseHandle(existing);
    }
}

void research_activation_status_reset_for_tests(std::uint64_t session_nonce) noexcept {
    close_status_mapping();
    g_session_nonce = 0;
    g_configured_max_samples = 0;
    g_pipeline_proof = false;
    zero_named_status(0);
    if (session_nonce != 0) {
        zero_named_status(session_nonce);
    }
}

bool research_activation_status_try_read(ResearchActivationStatusRecord& out,
                                         bool& found,
                                         std::uint64_t session_nonce) noexcept {
    found = false;
    out = {};
    wchar_t mapping_name[96]{};
    mapping_name_for(session_nonce, mapping_name, 96);
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, mapping_name);
    if (mapping == nullptr) {
        return true;
    }
    const auto* view = static_cast<const ResearchActivationStatusRecord*>(
        MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(ResearchActivationStatusRecord)));
    if (view == nullptr) {
        CloseHandle(mapping);
        return false;
    }
    out = *view;
    found = out.magic == kResearchActivationStatusMagic;
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    return true;
}

void research_activation_status_publish_bootstrap_complete() noexcept {
    if (!ensure_status_mapping()) {
        return;
    }
    g_status_view->bootstrap_complete = 1;
}

void research_activation_status_publish_runtime_state(std::uint32_t intent_state,
                                                      std::uint32_t research_substate,
                                                      std::uint32_t samples_collected,
                                                      bool observation_complete,
                                                      bool pipeline_proof,
                                                      std::uint64_t session_nonce) noexcept {
    if (!ensure_status_mapping()) {
        return;
    }
    g_status_view->magic = kResearchActivationStatusMagic;
    g_status_view->schema_version = kResearchActivationStatusSchemaVersion;
    g_status_view->intent_state = intent_state;
    g_status_view->research_substate = research_substate;
    g_status_view->samples_collected = samples_collected;
    g_status_view->observation_complete = observation_complete ? 1u : 0u;
    g_status_view->bootstrap_complete = 1;
    g_status_view->pipeline_proof = pipeline_proof ? 1u : 0u;
    g_status_view->session_nonce = session_nonce;
    g_status_view->configured_max_samples = g_configured_max_samples;
}

void research_activation_status_bind_session(bool pipeline_proof,
                                             std::uint64_t session_nonce,
                                             std::uint32_t configured_max_samples) noexcept {
    if (g_session_nonce != session_nonce) {
        close_status_mapping();
    }
    g_pipeline_proof = pipeline_proof;
    g_session_nonce = session_nonce;
    g_configured_max_samples = configured_max_samples;
}

void research_activation_status_publish_controller(std::uint32_t intent_state,
                                                   std::uint32_t research_substate,
                                                   std::uint32_t samples_collected,
                                                   bool observation_complete) noexcept {
    research_activation_status_publish_runtime_state(intent_state,
                                                     research_substate,
                                                     samples_collected,
                                                     observation_complete,
                                                     g_pipeline_proof,
                                                     g_session_nonce);
}

bool status_matches_session(const ResearchActivationStatusRecord& out,
                            std::uint32_t expected_research_substate,
                            std::uint32_t min_samples,
                            bool expect_observation_complete,
                            std::uint64_t session_nonce) noexcept {
    if (out.magic != kResearchActivationStatusMagic || out.bootstrap_complete == 0) {
        return false;
    }
    if (out.session_nonce != session_nonce) {
        return false;
    }
    if (out.configured_max_samples != 0 && out.samples_collected > out.configured_max_samples) {
        return false;
    }
    return out.research_substate == expected_research_substate
        && out.samples_collected >= min_samples
        && (!expect_observation_complete || out.observation_complete != 0);
}

bool research_activation_status_wait_for(std::uint32_t expected_research_substate,
                                         std::uint32_t min_samples,
                                         bool expect_observation_complete,
                                         std::uint32_t timeout_ms,
                                         std::uint64_t session_nonce,
                                         ResearchActivationStatusRecord& out) noexcept {
    const std::uint64_t deadline = steady_ms() + timeout_ms;
    while (steady_ms() < deadline) {
        bool found = false;
        if (!research_activation_status_try_read(out, found, session_nonce)) {
            return false;
        }
        if (found && status_matches_session(out,
                                            expected_research_substate,
                                            min_samples,
                                            expect_observation_complete,
                                            session_nonce)) {
            return true;
        }
        Sleep(10);
    }
    bool found = false;
    if (!research_activation_status_try_read(out, found, session_nonce)) {
        return false;
    }
    return found && status_matches_session(out,
                                           expected_research_substate,
                                           min_samples,
                                           expect_observation_complete,
                                           session_nonce);
}

} // namespace deflux::game::gta_sa_de
