#pragma once

// Task #9B Gate 2A: explicit research-only remote LoadLibraryW loader (M1).
// NOT production bootstrap. NOT a generic injector.

#include "deflux/core/process_lifecycle.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace deflux::game::gta_sa_de {

inline constexpr const char kResearchRuntimeDllFileName[] = "DeFlux_Runtime.dll";
inline constexpr const char kResearchLoaderFixtureExeName[] = "DeFlux_ResearchLoader_Fixture.exe";
inline constexpr std::uint32_t kResearchRemoteThreadWaitMs = 30000;
inline constexpr std::size_t kResearchRemotePathMaxChars = 32768;

// Minimal rights for CreateRemoteThread + LoadLibraryW path write (not PROCESS_ALL_ACCESS).
// PROCESS_CREATE_THREAD     — CreateRemoteThread for LoadLibraryW stub thread
// PROCESS_QUERY_INFORMATION — QueryFullProcessImageNameW / GetProcessTimes / IsWow64Process2
// PROCESS_VM_OPERATION      — VirtualAllocEx for UTF-16 path buffer
// PROCESS_VM_WRITE          — WriteProcessMemory for UTF-16 path buffer only
inline constexpr std::uint32_t kResearchInjectProcessAccess =
    0x00000002u /* PROCESS_CREATE_THREAD */ | 0x00000400u /* PROCESS_QUERY_INFORMATION */
    | 0x00000008u /* PROCESS_VM_OPERATION */ | 0x00000020u /* PROCESS_VM_WRITE */;

// Narrow rights for preflight / TOCTOU identity re-check only.
inline constexpr std::uint32_t kResearchQueryProcessAccess = 0x00001000u; // PROCESS_QUERY_LIMITED_INFORMATION

enum class ResearchLoadResult : std::uint32_t {
    LoadNotRequested = 0,
    TargetNotRunning,
    TargetIdentityMismatch,
    BuildNotAllowlisted,
    PreflightFailed,
    RuntimeDllNotFound,
    ArchitectureMismatch,
    OpenProcessFailed,
    RemoteAllocFailed,
    RemoteWriteFailed,
    LoadLibraryResolutionFailed,
    RemoteThreadFailed,
    RemoteThreadTimeout,
    RemoteLoadFailed,
    LoadSucceeded,
    CleanupFailed,
    ArbitraryTargetRejected,
    ArbitraryDllRejected,
};

enum class ResearchLoadTargetMode : std::uint8_t {
    GtaSaDeResearchOnly = 0,
    SyntheticFixtureOnly,
};

enum class ResearchLoadStage : std::uint8_t {
    None = 0,
    Preflight,
    TargetQuery,
    IdentityRecheck,
    RuntimePathResolve,
    OpenProcess,
    RemoteAlloc,
    RemoteWrite,
    ResolveLoadLibraryW,
    CreateRemoteThread,
    WaitThread,
    InspectResult,
    Cleanup,
};

struct ResearchLoadDiagnostics {
    ResearchLoadResult result = ResearchLoadResult::LoadNotRequested;
    ResearchLoadStage stage = ResearchLoadStage::None;
    std::uint32_t win32_error = 0;
    std::string message;
    std::string target_summary;
    std::string research_sha256;
    std::string runtime_dll_path;
    bool cleanup_complete = false;
    bool remote_path_freed = false;
    bool thread_handle_closed = false;
    bool process_handle_closed = false;
    bool duplicate_load_attempt = false;
};

struct ResearchInjectPreflight {
    std::string executable_path;
    std::string sha256_hex;
    bool environment_clean = false;
};

struct ResearchRemoteLoadRequest {
    ResearchLoadTargetMode target_mode = ResearchLoadTargetMode::GtaSaDeResearchOnly;
    bool load_requested = false;
    ResearchInjectPreflight preflight{};
    deflux::core::ProcessInstance observed_target{};
    deflux::core::ProcessInstance pre_load_target{};
    std::wstring runtime_dll_path;
    std::uint32_t remote_thread_wait_ms = kResearchRemoteThreadWaitMs;
};

const char* to_string(ResearchLoadResult result) noexcept;
const char* to_string(ResearchLoadStage stage) noexcept;

std::size_t research_remote_path_buffer_bytes(std::size_t path_char_count) noexcept;

bool serialize_utf16_le_path(std::wstring_view path,
                            std::vector<std::uint8_t>& out_bytes,
                            std::string& error_out) noexcept;

bool resolve_trusted_runtime_dll_path(std::wstring& out_path,
                                      const std::wstring& launcher_module_path,
                                      std::wstring_view gta_install_directory,
                                      std::string& error_out) noexcept;

bool validate_runtime_dll_outside_gta_directory(std::wstring_view runtime_dll_path,
                                                std::wstring_view gta_install_directory,
                                                std::string& error_out) noexcept;

bool query_research_target_identity(std::uint32_t process_id,
                                    deflux::core::ProcessInstance& out) noexcept;

ResearchLoadDiagnostics execute_research_remote_load(ResearchRemoteLoadRequest& request) noexcept;

// Test/harness seam: synthetic fixture target only.
ResearchLoadDiagnostics execute_research_remote_load_synthetic_fixture(
    const std::wstring& fixture_exe_path,
    const std::wstring& launcher_module_path,
    const std::wstring& runtime_dll_path_override,
    std::uint32_t target_process_id,
    deflux::core::ProcessInstance expected_identity) noexcept;

} // namespace deflux::game::gta_sa_de
