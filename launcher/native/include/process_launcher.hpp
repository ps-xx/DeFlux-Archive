#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shellapi.h>

#include <cstdint>
#include <string>

namespace deflux::launcher {

struct LaunchResult {
    bool ok = false;
    DWORD process_id = 0;
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
};

class ProcessLauncher {
public:
    // Starts an executable via CreateProcessW (often fails for Steam/RGL titles).
    static LaunchResult launch(const std::wstring& exe_path,
                               const std::wstring& working_directory,
                               const std::wstring& command_line);

    // Boots a Steam app (required for GTA SA Definitive Edition / Gameface).
    // optional_args are appended after the app id for steam.exe fallback
    // and encoded into steam://rungameid/<id>//<args>.
    static bool launch_steam_app(std::uint32_t app_id, const std::wstring& optional_args = {});

    static std::wstring find_steam_exe();

    static void close(LaunchResult& result);
};

} // namespace deflux::launcher
