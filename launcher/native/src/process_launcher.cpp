#include "process_launcher.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace deflux::launcher {

namespace {

std::wstring read_reg_string(HKEY root, const wchar_t* subkey, const wchar_t* value_name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return {};
    }

    wchar_t buffer[MAX_PATH]{};
    DWORD type = 0;
    DWORD size = sizeof(buffer);
    const LONG status = RegQueryValueExW(key, value_name, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);

    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return {};
    }
    return buffer;
}

} // namespace

std::wstring ProcessLauncher::find_steam_exe() {
    const wchar_t* candidates[] = {
        L"C:\\Program Files (x86)\\Steam\\steam.exe",
        L"C:\\Program Files\\Steam\\steam.exe",
    };
    for (const wchar_t* path : candidates) {
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
            return path;
        }
    }

    std::wstring from_reg = read_reg_string(
        HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamExe");
    if (!from_reg.empty() && GetFileAttributesW(from_reg.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return from_reg;
    }

    from_reg = read_reg_string(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (!from_reg.empty()) {
        if (from_reg.back() != L'\\' && from_reg.back() != L'/') {
            from_reg.push_back(L'\\');
        }
        from_reg += L"steam.exe";
        if (GetFileAttributesW(from_reg.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return from_reg;
        }
    }

    return {};
}

bool ProcessLauncher::launch_steam_app(std::uint32_t app_id, const std::wstring& optional_args) {
    // Prefer the steam:// URI — same path as clicking Play in the Steam client.
    // CreateProcess(steam.exe -applaunch) can leave Rockstar Launcher in a bad state
    // ("Unable to launch game, please try reinstalling the game.").
    wchar_t uri[512]{};
    if (!optional_args.empty()) {
        // steam://rungameid/APPID//args  (args space-separated, URI-encoded spaces as %20)
        std::wstring encoded = optional_args;
        for (std::size_t i = 0; i < encoded.size(); ++i) {
            if (encoded[i] == L' ') {
                encoded.replace(i, 1, L"%20");
                i += 2;
            }
        }
        std::swprintf(uri, 512, L"steam://rungameid/%u//%s", app_id, encoded.c_str());
    } else {
        std::swprintf(uri, 512, L"steam://rungameid/%u", app_id);
    }

    const HINSTANCE shell = ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
    const auto code = reinterpret_cast<std::uintptr_t>(shell);
    if (code > 32) {
        std::fwprintf(stdout,
                      L"[DeFlux_Launcher] Asked Steam to launch appid %u via:\n  %s\n",
                      app_id,
                      uri);
        return true;
    }

    std::fwprintf(stderr,
                  L"[DeFlux_Launcher] ShellExecute steam:// failed (code=%llu); falling back to steam.exe\n",
                  static_cast<unsigned long long>(code));

    const std::wstring steam = find_steam_exe();
    if (steam.empty()) {
        std::fwprintf(stderr, L"[DeFlux_Launcher] steam.exe not found. Install Steam or pass --direct.\n");
        return false;
    }

    std::wstring args = L"-applaunch ";
    args += std::to_wstring(app_id);
    if (!optional_args.empty()) {
        args.push_back(L' ');
        args += optional_args;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::wstring cmd = L"\"" + steam + L"\" " + args;
    std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end());
    mutable_cmd.push_back(L'\0');

    const BOOL created = CreateProcessW(
        steam.c_str(),
        mutable_cmd.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &si,
        &pi);

    if (!created) {
        std::fwprintf(stderr, L"[DeFlux_Launcher] Failed to start Steam (Win32=%lu)\n", GetLastError());
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    std::fwprintf(stdout,
                  L"[DeFlux_Launcher] Asked Steam to launch appid %u via:\n  %s %s\n",
                  app_id,
                  steam.c_str(),
                  args.c_str());
    return true;
}

LaunchResult ProcessLauncher::launch(const std::wstring& exe_path,
                                     const std::wstring& working_directory,
                                     const std::wstring& command_line) {
    LaunchResult result{};

    if (GetFileAttributesW(exe_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::fwprintf(stderr, L"[DeFlux_Launcher] Game executable not found:\n  %s\n", exe_path.c_str());
        return result;
    }

    if (!working_directory.empty()
        && GetFileAttributesW(working_directory.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::fwprintf(stderr, L"[DeFlux_Launcher] Working directory not found:\n  %s\n",
                      working_directory.c_str());
        return result;
    }

    std::vector<wchar_t> cmd;
    cmd.reserve(command_line.size() + 1);
    cmd.insert(cmd.end(), command_line.begin(), command_line.end());
    cmd.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    const wchar_t* cwd = working_directory.empty() ? nullptr : working_directory.c_str();

    const BOOL created = CreateProcessW(
        exe_path.c_str(),
        cmd.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        cwd,
        &si,
        &pi);

    if (!created) {
        std::fwprintf(stderr, L"[DeFlux_Launcher] CreateProcessW failed (Win32=%lu)\n", GetLastError());
        return result;
    }

    result.ok = true;
    result.process_id = pi.dwProcessId;
    result.process = pi.hProcess;
    result.thread = pi.hThread;
    return result;
}

void ProcessLauncher::close(LaunchResult& result) {
    if (result.thread != nullptr) {
        CloseHandle(result.thread);
        result.thread = nullptr;
    }
    if (result.process != nullptr) {
        CloseHandle(result.process);
        result.process = nullptr;
    }
    result.ok = false;
    result.process_id = 0;
}

} // namespace deflux::launcher
