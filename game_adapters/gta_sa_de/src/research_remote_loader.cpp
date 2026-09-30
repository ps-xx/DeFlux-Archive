#include "deflux/game/gta_sa_de/research_remote_loader.hpp"

#include "deflux/game/gta_sa_de/research_build_allowlist.hpp"
#include "deflux/core/process_lifecycle.hpp"
#include "discovery.hpp"
#include "environment_preflight.hpp"
#include "process_query.hpp"
#include "win_utf8.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <TlHelp32.h>

#include <cstring>
#include <string>

namespace deflux::game::gta_sa_de {
namespace {

struct ScopedHandle {
    HANDLE value = nullptr;
    ~ScopedHandle() {
        if (value != nullptr && value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }
};

ResearchLoadDiagnostics make_diag(ResearchLoadResult result,
                                  ResearchLoadStage stage,
                                  std::string message,
                                  std::uint32_t win32 = 0) {
    ResearchLoadDiagnostics diag{};
    diag.result = result;
    diag.stage = stage;
    diag.message = std::move(message);
    diag.win32_error = win32;
    return diag;
}

const char* kStageNames[] = {
    "NONE",           "PREFLIGHT",        "TARGET_QUERY",     "IDENTITY_RECHECK",
    "RUNTIME_PATH",   "OPEN_PROCESS",     "REMOTE_ALLOC",     "REMOTE_WRITE",
    "RESOLVE_LLW",    "CREATE_THREAD",    "WAIT_THREAD",      "INSPECT_RESULT",
    "CLEANUP",
};

bool is_fixture_image_name(std::wstring_view image_path) {
    const auto slash = image_path.find_last_of(L"\\/");
    const std::wstring_view file =
        slash == std::wstring_view::npos ? image_path : image_path.substr(slash + 1);
    return detail::equals_ignore_case(file, L"DeFlux_ResearchLoader_Fixture.exe");
}

bool query_process_instance(std::uint32_t pid, deflux::core::ProcessInstance& out) {
    out = {};
    std::wstring image;
    std::uint64_t creation = 0;
    if (!detail::query_process_identity(pid, image, creation)) {
        return false;
    }
    out.process_id = pid;
    out.creation_identity = creation;
    out.executable_path = detail::wide_to_utf8(image);
    return deflux::core::is_valid_process_instance(out);
}

bool process_still_matches(const deflux::core::ProcessInstance& expected) {
    deflux::core::ProcessInstance current{};
    if (!query_process_instance(expected.process_id, current)) {
        return false;
    }
    return deflux::core::same_process_instance(expected, current)
        && detail::executable_paths_equal(
               detail::utf8_to_wide(expected.executable_path),
               detail::utf8_to_wide(current.executable_path));
}

bool is_native_x64_process(HANDLE process, std::string& error_out) {
    USHORT process_machine = 0;
    USHORT native_machine = 0;
    if (!IsWow64Process2(process, &process_machine, &native_machine)) {
        error_out = "IsWow64Process2 failed for target process.";
        return false;
    }
#if defined(_M_X64) || defined(_M_AMD64)
    if (process_machine != IMAGE_FILE_MACHINE_UNKNOWN || native_machine != IMAGE_FILE_MACHINE_AMD64) {
        error_out = "Target process is not native x64.";
        return false;
    }
    return true;
#else
    error_out = "Research launcher must be x64.";
    return false;
#endif
}

bool open_module_snapshot(DWORD pid, ScopedHandle& snapshot) {
    for (int attempt = 0; attempt < 40; ++attempt) {
        snapshot.value = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
        if (snapshot.value != nullptr && snapshot.value != INVALID_HANDLE_VALUE) {
            return true;
        }
        snapshot.value = nullptr;
        Sleep(25);
    }
    return false;
}

bool resolve_remote_loadlibrary_w(HANDLE process, void*& remote_entry, std::string& error_out) {
    remote_entry = nullptr;
    HMODULE local_kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (local_kernel32 == nullptr) {
        error_out = "GetModuleHandleW(kernel32.dll) failed in launcher.";
        return false;
    }
    FARPROC local_loadlibrary = GetProcAddress(local_kernel32, "LoadLibraryW");
    if (local_loadlibrary == nullptr) {
        error_out = "GetProcAddress(LoadLibraryW) failed in launcher.";
        return false;
    }
    const std::uintptr_t offset =
        reinterpret_cast<std::uintptr_t>(local_loadlibrary)
        - reinterpret_cast<std::uintptr_t>(local_kernel32);

    const DWORD pid = GetProcessId(process);
    if (pid == 0) {
        error_out = "GetProcessId failed.";
        return false;
    }

    for (int attempt = 0; attempt < 40; ++attempt) {
        ScopedHandle snapshot;
        if (!open_module_snapshot(pid, snapshot)) {
            error_out = "CreateToolhelp32Snapshot(TH32CS_SNAPMODULE) failed.";
            return false;
        }

        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (!Module32FirstW(snapshot.value, &entry)) {
            error_out = "Module32FirstW failed for target process.";
            Sleep(25);
            continue;
        }

        do {
            if (_wcsicmp(entry.szModule, L"kernel32.dll") != 0) {
                continue;
            }
            remote_entry = reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(entry.modBaseAddr) + offset);
            return true;
        } while (Module32NextW(snapshot.value, &entry));

        error_out = "kernel32.dll not found in target process module list.";
        return false;
    }
    return false;
}

bool runtime_module_already_loaded(HANDLE process, std::wstring_view runtime_dll_path) {
    const DWORD pid = GetProcessId(process);
    if (pid == 0) {
        return false;
    }
    const std::wstring file_name = [&]() {
        const auto slash = runtime_dll_path.find_last_of(L"\\/");
        return slash == std::wstring_view::npos
            ? std::wstring(runtime_dll_path)
            : std::wstring(runtime_dll_path.substr(slash + 1));
    }();

    // A failed snapshot is not evidence that the module is absent. Retry only
    // that failure. A successful enumeration that does not contain the module
    // returns false immediately.
    for (int attempt = 0; attempt < 40; ++attempt) {
        ScopedHandle snapshot;
        if (!open_module_snapshot(pid, snapshot)) {
            return false;
        }
        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (!Module32FirstW(snapshot.value, &entry)) {
            Sleep(25);
            continue;
        }
        do {
            if (detail::equals_ignore_case(entry.szModule, file_name)) {
                return true;
            }
        } while (Module32NextW(snapshot.value, &entry));
        return false;
    }
    return false;
}

struct RemoteLoadState {
    ScopedHandle process;
    ScopedHandle thread;
    void* remote_path = nullptr;
    std::size_t remote_path_bytes = 0;
};

void cleanup_remote_load(RemoteLoadState& state, ResearchLoadDiagnostics& diag) {
    diag.stage = ResearchLoadStage::Cleanup;
    if (state.remote_path != nullptr && state.process.value != nullptr) {
        if (VirtualFreeEx(state.process.value, state.remote_path, 0, MEM_RELEASE)) {
            diag.remote_path_freed = true;
            state.remote_path = nullptr;
        } else if (diag.win32_error == 0) {
            diag.win32_error = static_cast<std::uint32_t>(GetLastError());
        }
    }
    if (state.thread.value != nullptr) {
        CloseHandle(state.thread.value);
        state.thread.value = nullptr;
        diag.thread_handle_closed = true;
    }
    if (state.process.value != nullptr) {
        CloseHandle(state.process.value);
        state.process.value = nullptr;
        diag.process_handle_closed = true;
    }
    diag.cleanup_complete =
        diag.remote_path_freed && diag.thread_handle_closed && diag.process_handle_closed;
}

ResearchLoadDiagnostics execute_remote_load_core(ResearchRemoteLoadRequest& request) {
    ResearchLoadDiagnostics diag{};
    diag.research_sha256 = request.preflight.sha256_hex;
    diag.runtime_dll_path = detail::wide_to_utf8(request.runtime_dll_path);

    if (!request.load_requested) {
        return make_diag(ResearchLoadResult::LoadNotRequested, ResearchLoadStage::None,
                         "Load not requested.");
    }
    if (!deflux::core::is_valid_process_instance(request.pre_load_target)) {
        return make_diag(ResearchLoadResult::TargetIdentityMismatch, ResearchLoadStage::TargetQuery,
                         "Pre-load target identity is invalid.");
    }
    if (request.runtime_dll_path.empty()) {
        return make_diag(ResearchLoadResult::RuntimeDllNotFound, ResearchLoadStage::RuntimePathResolve,
                         "Runtime DLL path is empty.");
    }

    diag.target_summary = "pid=" + std::to_string(request.pre_load_target.process_id)
        + " creation=" + std::to_string(request.pre_load_target.creation_identity)
        + " path=" + request.pre_load_target.executable_path;

    diag.stage = ResearchLoadStage::IdentityRecheck;
    if (!process_still_matches(request.pre_load_target)) {
        return make_diag(ResearchLoadResult::TargetIdentityMismatch, ResearchLoadStage::IdentityRecheck,
                         "Target process identity changed before load (TOCTOU).");
    }

    RemoteLoadState state{};
    diag.stage = ResearchLoadStage::OpenProcess;
    state.process.value =
        OpenProcess(kResearchInjectProcessAccess, FALSE, request.pre_load_target.process_id);
    if (state.process.value == nullptr) {
        const std::uint32_t err = static_cast<std::uint32_t>(GetLastError());
        diag.win32_error = err;
        diag.result = ResearchLoadResult::OpenProcessFailed;
        diag.message = "OpenProcess for injection failed.";
        return diag;
    }

    std::string arch_error;
    if (!is_native_x64_process(state.process.value, arch_error)) {
        diag.result = ResearchLoadResult::ArchitectureMismatch;
        diag.message = arch_error;
        cleanup_remote_load(state, diag);
        return diag;
    }

    diag.duplicate_load_attempt =
        runtime_module_already_loaded(state.process.value, request.runtime_dll_path);

    std::vector<std::uint8_t> path_bytes;
    std::string ser_error;
    if (!serialize_utf16_le_path(request.runtime_dll_path, path_bytes, ser_error)) {
        diag.result = ResearchLoadResult::RemoteWriteFailed;
        diag.stage = ResearchLoadStage::RemoteWrite;
        diag.message = ser_error;
        cleanup_remote_load(state, diag);
        return diag;
    }

    diag.stage = ResearchLoadStage::RemoteAlloc;
    state.remote_path_bytes = path_bytes.size();
    state.remote_path = VirtualAllocEx(state.process.value,
                                       nullptr,
                                       state.remote_path_bytes,
                                       MEM_COMMIT | MEM_RESERVE,
                                       PAGE_READWRITE);
    if (state.remote_path == nullptr) {
        diag.win32_error = static_cast<std::uint32_t>(GetLastError());
        diag.result = ResearchLoadResult::RemoteAllocFailed;
        diag.message = "VirtualAllocEx for DLL path failed.";
        cleanup_remote_load(state, diag);
        return diag;
    }

    diag.stage = ResearchLoadStage::RemoteWrite;
    SIZE_T written = 0;
    if (!WriteProcessMemory(state.process.value,
                            state.remote_path,
                            path_bytes.data(),
                            state.remote_path_bytes,
                            &written)
        || written != state.remote_path_bytes) {
        diag.win32_error = static_cast<std::uint32_t>(GetLastError());
        diag.result = ResearchLoadResult::RemoteWriteFailed;
        diag.message = "WriteProcessMemory for DLL path failed.";
        cleanup_remote_load(state, diag);
        return diag;
    }

    void* remote_loadlibrary = nullptr;
    std::string resolve_error;
    diag.stage = ResearchLoadStage::ResolveLoadLibraryW;
    if (!resolve_remote_loadlibrary_w(state.process.value, remote_loadlibrary, resolve_error)) {
        diag.result = ResearchLoadResult::LoadLibraryResolutionFailed;
        diag.message = resolve_error;
        cleanup_remote_load(state, diag);
        return diag;
    }

    diag.stage = ResearchLoadStage::CreateRemoteThread;
    state.thread.value = CreateRemoteThread(state.process.value,
                                            nullptr,
                                            0,
                                            reinterpret_cast<LPTHREAD_START_ROUTINE>(remote_loadlibrary),
                                            state.remote_path,
                                            0,
                                            nullptr);
    if (state.thread.value == nullptr) {
        diag.win32_error = static_cast<std::uint32_t>(GetLastError());
        diag.result = ResearchLoadResult::RemoteThreadFailed;
        diag.message = "CreateRemoteThread failed.";
        cleanup_remote_load(state, diag);
        return diag;
    }

    diag.stage = ResearchLoadStage::WaitThread;
    const DWORD wait = WaitForSingleObject(state.thread.value, request.remote_thread_wait_ms);
    if (wait == WAIT_TIMEOUT) {
        diag.result = ResearchLoadResult::RemoteThreadTimeout;
        diag.message =
            "Remote LoadLibraryW thread timed out; remote thread may still be running.";
        cleanup_remote_load(state, diag);
        if (!diag.cleanup_complete) {
            diag.result = ResearchLoadResult::CleanupFailed;
        }
        return diag;
    }
    if (wait != WAIT_OBJECT_0) {
        diag.win32_error = static_cast<std::uint32_t>(GetLastError());
        diag.result = ResearchLoadResult::RemoteThreadFailed;
        diag.message = "WaitForSingleObject failed for remote thread.";
        cleanup_remote_load(state, diag);
        return diag;
    }

    diag.stage = ResearchLoadStage::InspectResult;
    DWORD exit_code = 0;
    if (!GetExitCodeThread(state.thread.value, &exit_code)) {
        diag.win32_error = static_cast<std::uint32_t>(GetLastError());
        diag.result = ResearchLoadResult::RemoteLoadFailed;
        diag.message = "GetExitCodeThread failed.";
        cleanup_remote_load(state, diag);
        return diag;
    }
    if (exit_code == 0) {
        diag.result = ResearchLoadResult::RemoteLoadFailed;
        diag.message = "Remote LoadLibraryW returned NULL.";
        cleanup_remote_load(state, diag);
        return diag;
    }

    diag.result = ResearchLoadResult::LoadSucceeded;
    diag.message = diag.duplicate_load_attempt
        ? "DeFlux_Runtime.dll load succeeded (module was already present)."
        : "DeFlux_Runtime.dll load succeeded.";
    cleanup_remote_load(state, diag);
    if (!diag.cleanup_complete) {
        diag.result = ResearchLoadResult::CleanupFailed;
        diag.message += " Cleanup incomplete.";
    }
    return diag;
}

} // namespace

const char* to_string(ResearchLoadResult result) noexcept {
    switch (result) {
    case ResearchLoadResult::LoadNotRequested:
        return "LOAD_NOT_REQUESTED";
    case ResearchLoadResult::TargetNotRunning:
        return "TARGET_NOT_RUNNING";
    case ResearchLoadResult::TargetIdentityMismatch:
        return "TARGET_IDENTITY_MISMATCH";
    case ResearchLoadResult::BuildNotAllowlisted:
        return "BUILD_NOT_ALLOWLISTED";
    case ResearchLoadResult::PreflightFailed:
        return "PREFLIGHT_FAILED";
    case ResearchLoadResult::RuntimeDllNotFound:
        return "RUNTIME_DLL_NOT_FOUND";
    case ResearchLoadResult::ArchitectureMismatch:
        return "ARCHITECTURE_MISMATCH";
    case ResearchLoadResult::OpenProcessFailed:
        return "OPEN_PROCESS_FAILED";
    case ResearchLoadResult::RemoteAllocFailed:
        return "REMOTE_ALLOC_FAILED";
    case ResearchLoadResult::RemoteWriteFailed:
        return "REMOTE_WRITE_FAILED";
    case ResearchLoadResult::LoadLibraryResolutionFailed:
        return "LOADLIBRARY_RESOLUTION_FAILED";
    case ResearchLoadResult::RemoteThreadFailed:
        return "REMOTE_THREAD_FAILED";
    case ResearchLoadResult::RemoteThreadTimeout:
        return "REMOTE_THREAD_TIMEOUT";
    case ResearchLoadResult::RemoteLoadFailed:
        return "REMOTE_LOAD_FAILED";
    case ResearchLoadResult::LoadSucceeded:
        return "LOAD_SUCCEEDED";
    case ResearchLoadResult::CleanupFailed:
        return "CLEANUP_FAILED";
    case ResearchLoadResult::ArbitraryTargetRejected:
        return "ARBITRARY_TARGET_REJECTED";
    case ResearchLoadResult::ArbitraryDllRejected:
        return "ARBITRARY_DLL_REJECTED";
    }
    return "UNKNOWN";
}

const char* to_string(ResearchLoadStage stage) noexcept {
    const auto idx = static_cast<std::size_t>(stage);
    if (idx >= sizeof(kStageNames) / sizeof(kStageNames[0])) {
        return "UNKNOWN";
    }
    return kStageNames[idx];
}

std::size_t research_remote_path_buffer_bytes(std::size_t path_char_count) noexcept {
    if (path_char_count > kResearchRemotePathMaxChars) {
        return 0;
    }
    return (path_char_count + 1) * sizeof(wchar_t);
}

bool serialize_utf16_le_path(std::wstring_view path,
                            std::vector<std::uint8_t>& out_bytes,
                            std::string& error_out) noexcept {
    error_out.clear();
    out_bytes.clear();
    if (path.empty() || path.size() > kResearchRemotePathMaxChars) {
        error_out = "DLL path length is out of bounds.";
        return false;
    }
    const std::size_t bytes = research_remote_path_buffer_bytes(path.size());
    if (bytes == 0) {
        error_out = "Remote path buffer size calculation failed.";
        return false;
    }
    out_bytes.resize(bytes, 0);
    std::memcpy(out_bytes.data(), path.data(), path.size() * sizeof(wchar_t));
    return true;
}

bool resolve_trusted_runtime_dll_path(std::wstring& out_path,
                                      const std::wstring& launcher_module_path,
                                      std::wstring_view gta_install_directory,
                                      std::string& error_out) noexcept {
    error_out.clear();
    out_path.clear();
    const auto slash = launcher_module_path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        error_out = "Launcher module path is invalid.";
        return false;
    }
    const std::wstring candidate =
        launcher_module_path.substr(0, slash + 1)
        + detail::utf8_to_wide(kResearchRuntimeDllFileName);
    const std::wstring canonical = detail::canonicalize_file_path(candidate);
    if (canonical.empty()) {
        error_out = "DeFlux_Runtime.dll was not found beside the research launcher.";
        return false;
    }
    if (!validate_runtime_dll_outside_gta_directory(canonical, gta_install_directory, error_out)) {
        return false;
    }
    out_path = canonical;
    return true;
}

bool validate_runtime_dll_outside_gta_directory(std::wstring_view runtime_dll_path,
                                                std::wstring_view gta_install_directory,
                                                std::string& error_out) noexcept {
    error_out.clear();
    if (runtime_dll_path.empty()) {
        error_out = "Runtime DLL path is empty.";
        return false;
    }
    const std::wstring canonical_runtime = detail::canonicalize_file_path(std::wstring(runtime_dll_path));
    if (canonical_runtime.empty()) {
        error_out = "Runtime DLL path is not a regular file.";
        return false;
    }
    if (gta_install_directory.empty()) {
        return true;
    }
    const auto slash = gta_install_directory.find_last_of(L"\\/");
    const std::wstring gta_dir =
        slash == std::wstring_view::npos
        ? std::wstring(gta_install_directory)
        : std::wstring(gta_install_directory.substr(0, slash));
    const std::wstring canonical_gta_dir = detail::canonicalize_file_path(gta_dir);
    if (canonical_gta_dir.empty()) {
        return true;
    }
    if (canonical_runtime.size() >= canonical_gta_dir.size()
        && detail::equals_ignore_case(
               canonical_runtime.substr(0, canonical_gta_dir.size()),
               canonical_gta_dir)
        && (canonical_runtime.size() == canonical_gta_dir.size()
            || canonical_runtime[canonical_gta_dir.size()] == L'\\'
            || canonical_runtime[canonical_gta_dir.size()] == L'/')) {
        error_out = "Runtime DLL must not be sourced from the GTA install directory.";
        return false;
    }
    return true;
}

bool query_research_target_identity(std::uint32_t process_id,
                                    deflux::core::ProcessInstance& out) noexcept {
    return query_process_instance(process_id, out);
}

ResearchLoadDiagnostics execute_research_remote_load(ResearchRemoteLoadRequest& request) noexcept {
    return execute_remote_load_core(request);
}

ResearchLoadDiagnostics execute_research_remote_load_synthetic_fixture(
    const std::wstring& fixture_exe_path,
    const std::wstring& launcher_module_path,
    const std::wstring& runtime_dll_path_override,
    std::uint32_t target_process_id,
    deflux::core::ProcessInstance expected_identity) noexcept {
    if (!is_fixture_image_name(fixture_exe_path)) {
        return make_diag(ResearchLoadResult::ArbitraryTargetRejected, ResearchLoadStage::TargetQuery,
                         "Synthetic loader rejected non-fixture target image.");
    }
    ResearchRemoteLoadRequest request{};
    request.target_mode = ResearchLoadTargetMode::SyntheticFixtureOnly;
    request.load_requested = true;
    request.preflight.executable_path = detail::wide_to_utf8(fixture_exe_path);
    request.preflight.sha256_hex = "synthetic_fixture";
    request.preflight.environment_clean = true;
    request.pre_load_target = expected_identity;
    request.pre_load_target.process_id = target_process_id;
    request.pre_load_target.executable_path = detail::wide_to_utf8(fixture_exe_path);
    request.runtime_dll_path = runtime_dll_path_override.empty()
        ? [&]() -> std::wstring {
              std::wstring path;
              std::string err;
              if (!resolve_trusted_runtime_dll_path(path, launcher_module_path, L"", err)) {
                  return {};
              }
              return path;
          }()
        : detail::canonicalize_file_path(runtime_dll_path_override);
    return execute_remote_load_core(request);
}

} // namespace deflux::game::gta_sa_de
