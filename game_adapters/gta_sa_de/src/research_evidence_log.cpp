#include "deflux/game/gta_sa_de/research_evidence_log.hpp"

#include "win_utf8.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <ShlObj.h>

#include <cstdio>
#include <mutex>
#include <share.h>
#include <string>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

namespace deflux::game::gta_sa_de {
namespace {

std::mutex g_log_mutex;
bool g_hook_enabled = false;
bool g_session_open = false;
std::string g_log_path;
std::string g_directory_override;
FILE* g_log_file = nullptr;

void close_file_locked() {
    if (g_log_file != nullptr) {
        std::fclose(g_log_file);
        g_log_file = nullptr;
    }
    g_session_open = false;
    g_log_path.clear();
}

std::string resolve_default_directory_locked() {
    if (!g_directory_override.empty()) {
        return g_directory_override;
    }
    PWSTR wide_path = nullptr;
    std::string result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &wide_path))
        && wide_path != nullptr) {
        result = detail::wide_to_utf8(wide_path);
        CoTaskMemFree(wide_path);
    }
    if (!result.empty() && result.back() != '\\' && result.back() != '/') {
        result.push_back('\\');
    }
    result += "DeFlux\\research";
    return result;
}

} // namespace

void research_evidence_set_hook_enabled(bool enabled) noexcept {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_hook_enabled = enabled;
}

void research_evidence_set_output_directory_override(const std::string& path) noexcept {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_directory_override = path;
}

void research_evidence_clear_output_directory_override() noexcept {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_directory_override.clear();
}

std::string research_evidence_default_directory_utf8() noexcept {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    return resolve_default_directory_locked();
}

bool research_evidence_begin_session(const ResearchEvidenceSessionHeader& header,
                                     std::string& log_path_out) noexcept {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    close_file_locked();

    const std::string dir = resolve_default_directory_locked();
    CreateDirectoryW(detail::utf8_to_wide(dir).c_str(), nullptr);

    char file_name[128]{};
    std::snprintf(file_name,
                  sizeof(file_name),
                  "\\ggamestate_observation_%u_%llu.log",
                  header.process_id,
                  static_cast<unsigned long long>(header.session_nonce));
    g_log_path = dir + file_name;
    const std::wstring wide_log_path = detail::utf8_to_wide(g_log_path);
    g_log_file = _wfsopen(wide_log_path.c_str(), L"a", _SH_DENYNO);
    if (g_log_file == nullptr) {
        g_log_path.clear();
        return false;
    }
    g_session_open = true;
    log_path_out = g_log_path;
    std::fprintf(g_log_file,
                 "SESSION_START pid=%u creation=%llu nonce=%llu experiment=%s version=%u "
                 "expected_sha=%s observed_sha=%s pipeline_proof=%d\n",
                 header.process_id,
                 static_cast<unsigned long long>(header.creation_identity),
                 static_cast<unsigned long long>(header.session_nonce),
                 header.experiment_id != nullptr ? header.experiment_id : "",
                 header.experiment_version,
                 header.expected_sha256.c_str(),
                 header.observed_sha256.c_str(),
                 header.pipeline_proof ? 1 : 0);
    std::fflush(g_log_file);
    return true;
}

void research_evidence_append_sample(const InProcessResearchEvidenceSample& sample) noexcept {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_hook_enabled || !g_session_open || g_log_file == nullptr) {
        return;
    }
    std::fprintf(g_log_file,
                 "SAMPLE index=%u time_ms=%llu read_ok=%d byte=%u substate=%s derivation=%s "
                 "rva=0x%08X failure=%s\n",
                 sample.sample_index,
                 static_cast<unsigned long long>(sample.monotonic_time_ms),
                 sample.read_ok ? 1 : 0,
                 static_cast<unsigned>(sample.raw_byte),
                 to_string(sample.substate),
                 to_string(sample.derivation_method),
                 sample.derived_global_rva,
                 sample.failure_reason.c_str());
    std::fflush(g_log_file);
}

void research_evidence_end_session(ResearchSubstate final_substate,
                                   InProcessSignalClassification classification,
                                   const std::string& reason) noexcept {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_session_open || g_log_file == nullptr) {
        return;
    }
    std::fprintf(g_log_file,
                 "SESSION_END substate=%s classification=%s reason=%s\n",
                 to_string(final_substate),
                 to_string(classification),
                 reason.c_str());
    std::fflush(g_log_file);
    close_file_locked();
}

void research_evidence_append_lifecycle(const char* event, const char* detail) noexcept {
    if (event == nullptr || event[0] == '\0') {
        return;
    }
    const char* safe_detail = detail != nullptr ? detail : "";
    const DWORD pid = GetCurrentProcessId();
    const ULONGLONG tick = GetTickCount64();

    std::lock_guard<std::mutex> lock(g_log_mutex);
    const std::string dir = resolve_default_directory_locked();
    CreateDirectoryW(detail::utf8_to_wide(dir).c_str(), nullptr);

    char lifecycle_name[96]{};
    std::snprintf(lifecycle_name,
                  sizeof(lifecycle_name),
                  "\\lifecycle_%lu.log",
                  static_cast<unsigned long>(pid));
    const std::string lifecycle_path = dir + lifecycle_name;
    if (FILE* lifecycle = _wfsopen(detail::utf8_to_wide(lifecycle_path).c_str(),
                                   L"a",
                                   _SH_DENYNO);
        lifecycle != nullptr) {
        std::fprintf(lifecycle,
                     "BREADCRUMB tick=%llu pid=%lu event=%s detail=%s\n",
                     static_cast<unsigned long long>(tick),
                     static_cast<unsigned long>(pid),
                     event,
                     safe_detail);
        std::fflush(lifecycle);
        std::fclose(lifecycle);
    }

    if (g_hook_enabled && g_session_open && g_log_file != nullptr) {
        std::fprintf(g_log_file,
                     "BREADCRUMB tick=%llu event=%s detail=%s\n",
                     static_cast<unsigned long long>(tick),
                     event,
                     safe_detail);
        std::fflush(g_log_file);
    }
}

} // namespace deflux::game::gta_sa_de
