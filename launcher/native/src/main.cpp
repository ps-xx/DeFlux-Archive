#include "clean_save_deployer.hpp"
#include "intro_skip.hpp"
#include "process_launcher.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <TlHelp32.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kSadeSteamAppId = 1547000;
constexpr wchar_t kDefaultAsiDir[] =
    L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\"
    L"GTA San Andreas - The Definitive Edition\\Gameface\\Binaries\\Win64";

void print_usage() {
    std::wcout
        << L"DeFlux_Launcher\n"
        << L"Usage:\n"
        << L"  DeFlux_Launcher --steam-app 1547000 [--asi-dir <Win64 dir>]\n"
        << L"  DeFlux_Launcher --game <path-to-SanAndreas.exe> [--cwd <dir>]\n"
        << L"  DeFlux_Launcher --game <path> --direct   (bypass Steam; DE will exit)\n"
        << L"  DeFlux_Launcher --nui-only\n"
        << L"\n"
        << L"Intro / freeroam:\n"
        << L"  Rockstar logo / UE splash are skipped permanently by default.\n"
        << L"  Default entry: clean freeroam -> Slot 8 (GTASAsf8.sav) + SkipMenu=1.\n"
        << L"    (Slots 1-7 and GTSAautosave.sav are never touched.)\n"
        << L"  Use --new-game for StartNewGameFromMenu (story saves quarantined).\n"
        << L"  Use --keep-saves to Resume the user's existing SaveSlot as-is.\n"
        << L"  Use --keep-intro to leave Rockstar media untouched.\n"
        << L"\n"
        << L"Deploy build\\bin\\Release\\legacy\\version.dll + DeFluxCore.asi into Win64.\n";
}

std::wstring get_arg(int argc, wchar_t** argv, const std::wstring& key) {
    for (int i = 1; i < argc - 1; ++i) {
        if (key == argv[i]) {
            return argv[i + 1];
        }
    }
    return {};
}

bool has_flag(int argc, wchar_t** argv, const std::wstring& key) {
    for (int i = 1; i < argc; ++i) {
        if (key == argv[i]) {
            return true;
        }
    }
    return false;
}

bool looks_like_sade(const fs::path& exe) {
    const std::wstring name = exe.filename().wstring();
    if (_wcsicmp(name.c_str(), L"SanAndreas.exe") == 0) {
        return true;
    }
    if (_wcsicmp(name.c_str(), L"Gameface-Win64-Shipping.exe") == 0) {
        return true;
    }
    const std::wstring full = exe.wstring();
    return full.find(L"GTA San Andreas - The Definitive Edition") != std::wstring::npos
        || full.find(L"Gameface") != std::wstring::npos;
}

void warn_asi_dir(const fs::path& asi_dir) {
    const fs::path asi_path = asi_dir / L"DeFluxCore.asi";
    if (GetFileAttributesW(asi_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wcerr << L"[DeFlux_Launcher] Warning: DeFluxCore.asi not found in:\n  "
                   << asi_path.wstring() << L"\n";
    } else {
        std::wcout << L"[DeFlux_Launcher] Found DeFluxCore.asi: " << asi_path.wstring() << L"\n";
    }
}

std::uint32_t parse_u32(const std::wstring& text, std::uint32_t fallback) {
    if (text.empty()) {
        return fallback;
    }
    try {
        return static_cast<std::uint32_t>(std::stoul(text));
    } catch (...) {
    }
    return fallback;
}

std::wstring pick_content_hint(const std::wstring& asi_dir,
                               const std::wstring& cwd,
                               const std::wstring& game) {
    if (!asi_dir.empty()) {
        return asi_dir;
    }
    if (!cwd.empty()) {
        return cwd;
    }
    if (!game.empty()) {
        return fs::path(game).parent_path().wstring();
    }
    if (GetFileAttributesW(kDefaultAsiDir) != INVALID_FILE_ATTRIBUTES) {
        return kDefaultAsiDir;
    }
    return {};
}

bool is_sade_game_process_name(const wchar_t* name) {
    return _wcsicmp(name, L"SanAndreas.exe") == 0
        || _wcsicmp(name, L"Gameface-Win64-Shipping.exe") == 0;
}

DWORD find_sade_game_pid() {
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return 0;
    }
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (is_sade_game_process_name(pe.szExeFile)) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

bool game_window_visible() {
    return FindWindowW(L"UnrealWindow", nullptr) != nullptr
        || FindWindowW(nullptr, L"Grand Theft Auto: San Andreas - The Definitive Edition") != nullptr;
}

void write_entry_mode_marker(const std::wstring& win64_hint, const char* mode) {
    fs::path dir = win64_hint.empty() ? fs::path(kDefaultAsiDir) : fs::path(win64_hint);
    // Climb to Win64 if needed.
    for (int i = 0; i < 4 && !dir.empty(); ++i) {
        if (fs::exists(dir / L"SanAndreas.exe") || fs::exists(dir / L"DeFluxCore.asi")) {
            break;
        }
        if (fs::exists(dir / L"Gameface" / L"Binaries" / L"Win64" / L"SanAndreas.exe")) {
            dir = dir / L"Gameface" / L"Binaries" / L"Win64";
            break;
        }
        dir = dir.parent_path();
    }
    const fs::path marker = dir / L"deflux_entry_mode.txt";
    std::error_code ec;
    fs::create_directories(dir, ec);
    FILE* f = nullptr;
    if (_wfopen_s(&f, marker.wstring().c_str(), L"wb") == 0 && f != nullptr) {
        std::fwrite(mode, 1, std::strlen(mode), f);
        std::fclose(f);
        std::wcout << L"[DeFlux_Launcher] Wrote entry mode marker: " << marker.wstring()
                   << L" (" << mode << L")\n";
    }
}

void wait_for_sade_session_then_restore(bool restore_story, bool restore_clean) {
    if (!restore_story && !restore_clean) {
        return;
    }

    std::wcout << L"[DeFlux_Launcher] Waiting for SanAndreas.exe / Gameface "
                  L"(holding save overrides)...\n";

    DWORD pid = 0;
    for (int i = 0; i < 300 && pid == 0; ++i) {
        pid = find_sade_game_pid();
        if (pid == 0) {
            Sleep(1000);
        }
    }
    if (pid == 0) {
        std::wcerr << L"[DeFlux_Launcher] Game exe not seen; restoring saves now.\n";
        if (restore_story) {
            (void)deflux::launcher::restore_story_saves_after_session();
        }
        if (restore_clean) {
            (void)deflux::launcher::restore_clean_freeroam_saves();
        }
        return;
    }

    for (int i = 0; i < 180 && !game_window_visible(); ++i) {
        Sleep(1000);
        const DWORD again = find_sade_game_pid();
        if (again != 0) {
            pid = again;
        }
    }

    std::wcout << L"[DeFlux_Launcher] Tracking game PID " << pid
               << L". Save overrides stay until this process exits.\n";

    HANDLE proc = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (proc == nullptr) {
        while (find_sade_game_pid() != 0 || game_window_visible()) {
            Sleep(2000);
        }
    } else {
        WaitForSingleObject(proc, INFINITE);
        CloseHandle(proc);
        Sleep(3000);
        while (find_sade_game_pid() != 0 || game_window_visible()) {
            Sleep(2000);
        }
    }

    std::wcout << L"[DeFlux_Launcher] Game exited â€” restoring user saves.\n";
    if (restore_story) {
        (void)deflux::launcher::restore_story_saves_after_session();
    }
    if (restore_clean) {
        (void)deflux::launcher::restore_clean_freeroam_saves();
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (has_flag(argc, argv, L"--nui-only")) {
        std::wcout << L"[DeFlux_Launcher] NUI host mode: start Node host via `npm start` in launcher/\n";
        return 0;
    }

    const std::wstring steam_app_arg = get_arg(argc, argv, L"--steam-app");
    const std::wstring game = get_arg(argc, argv, L"--game");
    std::wstring cwd = get_arg(argc, argv, L"--cwd");
    std::wstring asi_dir = get_arg(argc, argv, L"--asi-dir");
    const bool force_direct = has_flag(argc, argv, L"--direct");
    const bool keep_intro = has_flag(argc, argv, L"--keep-intro");
    const bool keep_saves = has_flag(argc, argv, L"--keep-saves");
    const bool force_new_game = has_flag(argc, argv, L"--new-game");
    const bool skip_intro = !keep_intro;

    // Default: clean freeroam save deploy + SkipMenu resume.
    using Mode = deflux::launcher::FusionFixEntryMode;
    Mode entry_mode = Mode::CleanSave;
    if (keep_saves) {
        entry_mode = Mode::KeepSaves;
    } else if (force_new_game) {
        entry_mode = Mode::NewGame;
    }

    if (steam_app_arg.empty() && game.empty()) {
        print_usage();
        return 1;
    }

    if (!asi_dir.empty()) {
        warn_asi_dir(fs::path(asi_dir));
    } else if (!cwd.empty()) {
        warn_asi_dir(fs::path(cwd));
    } else if (!game.empty()) {
        warn_asi_dir(fs::path(game).parent_path());
    }

    if (skip_intro) {
        const std::wstring hint = pick_content_hint(asi_dir, cwd, game);
        if (!hint.empty()) {
            (void)deflux::launcher::apply_permanent_rockstar_intro_skip(hint);
        } else {
            std::wcerr << L"[DeFlux_Launcher] Intro skip: pass --asi-dir <Win64> so media can be patched.\n";
        }
    } else {
        std::wcout << L"[DeFlux_Launcher] Keeping Rockstar intro media (--keep-intro).\n";
    }

    (void)deflux::launcher::apply_freeroam_empty_world_settings();

    const std::wstring fusion_hint = !asi_dir.empty() ? asi_dir
        : (!cwd.empty() ? cwd
                        : (!game.empty() ? fs::path(game).parent_path().wstring() : kDefaultAsiDir));
    (void)deflux::launcher::configure_fusionfix_entry_mode(fusion_hint, entry_mode);

    bool quarantined = false;
    bool clean_deployed = false;

    if (entry_mode == Mode::CleanSave) {
        // Slot 8 only â€” never quarantine / touch autosave or story slots 1-7.
        clean_deployed = deflux::launcher::deploy_clean_freeroam_saves();
        write_entry_mode_marker(fusion_hint, "clean_save");
        if (!clean_deployed) {
            std::wcerr << L"[DeFlux_Launcher] WARNING: Slot 8 clean deploy failed; "
                          L"SkipMenu may resume last story save.\n";
        }
    } else if (entry_mode == Mode::NewGame) {
        quarantined = deflux::launcher::quarantine_story_saves_for_new_game();
        write_entry_mode_marker(fusion_hint, "new_game");
    } else {
        std::wcout << L"[DeFlux_Launcher] --keep-saves: FusionFix SkipMenu will resume SaveSlot.\n";
        write_entry_mode_marker(fusion_hint, "keep_saves");
    }

    const bool use_steam = !force_direct
        && (!steam_app_arg.empty() || (!game.empty() && looks_like_sade(fs::path(game))));

    auto restore_on_fail = [&]() {
        if (quarantined) {
            (void)deflux::launcher::restore_story_saves_after_session();
        }
        if (clean_deployed) {
            (void)deflux::launcher::restore_clean_freeroam_saves();
        }
    };

    if (use_steam) {
        const std::uint32_t app_id = parse_u32(steam_app_arg, kSadeSteamAppId);
        std::wcout << L"[DeFlux_Launcher] Launching via Steam appid " << app_id << L"...\n";
        if (!deflux::launcher::ProcessLauncher::launch_steam_app(app_id, L"-nosplash")) {
            restore_on_fail();
            return 1;
        }
        const wchar_t* mode_text = L"Clean freeroam Slot 8 (GTASAsf8.sav + SkipMenu).\n";
        if (entry_mode == Mode::NewGame) {
            mode_text = L"New Game (saves quarantined + StartNewGameFromMenu).\n";
        } else if (entry_mode == Mode::KeepSaves) {
            mode_text = L"Resume SaveSlot via FusionFix SkipMenu.\n";
        }
        std::wcout
            << L"[DeFlux_Launcher] Steam launch requested.\n"
            << L"[DeFlux_Launcher] Session mode: " << mode_text
            << L"[DeFlux_Launcher] Leave this console open until you quit the game.\n";

        wait_for_sade_session_then_restore(quarantined, clean_deployed);
        return 0;
    }

    if (game.empty()) {
        restore_on_fail();
        print_usage();
        return 1;
    }

    const fs::path game_path(game);
    if (cwd.empty()) {
        cwd = game_path.parent_path().wstring();
    }

    std::wcout << L"[DeFlux_Launcher] Game: " << game << L"\n";
    std::wcout << L"[DeFlux_Launcher] CWD : " << cwd << L"\n";
    std::wcout << L"[DeFlux_Launcher] Launching game (direct CreateProcess)...\n";

    // Clean save already deployed above â€” CreateProcessW starts SanAndreas.exe.
    auto launched = deflux::launcher::ProcessLauncher::launch(game, cwd, L"\"" + game + L"\" -nosplash");
    if (!launched.ok) {
        std::wcerr << L"[DeFlux_Launcher] Failed to launch game process.\n";
        restore_on_fail();
        return 1;
    }

    std::wcout << L"[DeFlux_Launcher] Game launched successfully (PID " << launched.process_id << L").\n";
    if ((quarantined || clean_deployed) && launched.process != nullptr) {
        WaitForSingleObject(launched.process, INFINITE);
        if (quarantined) {
            (void)deflux::launcher::restore_story_saves_after_session();
        }
        if (clean_deployed) {
            (void)deflux::launcher::restore_clean_freeroam_saves();
        }
    }
    deflux::launcher::ProcessLauncher::close(launched);
    return 0;
}
