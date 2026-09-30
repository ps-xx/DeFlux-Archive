#include "intro_skip.hpp"
#include "clean_save_deployer.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <ShlObj.h>

#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#pragma comment(lib, "Shell32.lib")

namespace fs = std::filesystem;


namespace deflux::launcher {
namespace {

// Tiny stub MP4 (ftyp + free + empty mdat). Replaces the Rockstar stinger so the
// movie player finishes instantly instead of playing the full logo bumper.
constexpr unsigned char kBlankMp4[] = {
    0x00, 0x00, 0x00, 0x18, 0x66, 0x74, 0x79, 0x70, 0x6D, 0x70, 0x34, 0x32, 0x00, 0x00, 0x00, 0x00,
    0x6D, 0x70, 0x34, 0x32, 0x69, 0x73, 0x6F, 0x6D, 0x00, 0x00, 0x00, 0x08, 0x66, 0x72, 0x65, 0x65,
    0x00, 0x00, 0x00, 0x08, 0x6D, 0x64, 0x61, 0x74,
};

// 1x1 black 24-bit BMP (UE Splash.bmp replacement).
constexpr unsigned char kBlackBmp[] = {
    0x42, 0x4D, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

bool write_bytes(const fs::path& path, const unsigned char* data, std::size_t size) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(out);
}

bool backup_and_replace(const fs::path& target,
                        const unsigned char* data,
                        std::size_t size,
                        const wchar_t* label) {
    if (!fs::exists(target)) {
        std::fwprintf(stdout, L"[DeFlux_Launcher] Intro skip: %s not found (ok):\n  %s\n",
                      label, target.wstring().c_str());
        return true;
    }

    const fs::path bak = fs::path(target.wstring() + L".deflux_bak");
    std::error_code ec;
    const auto cur_size = fs::file_size(target, ec);
    if (!ec && cur_size <= size + 64) {
        // Already looks like our stub — leave it.
        std::fwprintf(stdout, L"[DeFlux_Launcher] Intro skip: %s already stubbed.\n", label);
        return true;
    }

    if (!fs::exists(bak)) {
        fs::rename(target, bak, ec);
        if (ec) {
            // Fallback copy+overwrite if rename fails (file locked rare at launch).
            fs::copy_file(target, bak, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                std::fwprintf(stderr,
                              L"[DeFlux_Launcher] Intro skip: failed to backup %s (%hs)\n",
                              label,
                              ec.message().c_str());
                return false;
            }
        }
        std::fwprintf(stdout, L"[DeFlux_Launcher] Intro skip: backed up %s -> *.deflux_bak\n", label);
    }

    if (!write_bytes(target, data, size)) {
        std::fwprintf(stderr, L"[DeFlux_Launcher] Intro skip: failed to write stub for %s\n", label);
        return false;
    }

    std::fwprintf(stdout, L"[DeFlux_Launcher] Intro skip: replaced %s with instant stub.\n", label);
    return true;
}

fs::path resolve_gameface_content(const fs::path& hint) {
    // Accept Win64, Binaries, Gameface, or game root.
    fs::path p = hint;
    if (p.empty()) {
        return {};
    }

    // Climb a few levels looking for Gameface/Content.
    for (int i = 0; i < 6 && !p.empty(); ++i) {
        const fs::path content = p / L"Content";
        const fs::path movies = content / L"Movies";
        if (fs::exists(movies)) {
            return content;
        }
        const fs::path via_gameface = p / L"Gameface" / L"Content";
        if (fs::exists(via_gameface / L"Movies")) {
            return via_gameface;
        }
        if (p.has_parent_path() && p.parent_path() != p) {
            p = p.parent_path();
        } else {
            break;
        }
    }
    return {};
}

} // namespace

bool apply_permanent_rockstar_intro_skip(const std::wstring& gameface_or_win64_dir) {
    const fs::path content = resolve_gameface_content(gameface_or_win64_dir);
    if (content.empty()) {
        std::fwprintf(stderr,
                      L"[DeFlux_Launcher] Intro skip: could not locate Gameface\\Content from:\n  %s\n",
                      gameface_or_win64_dir.c_str());
        return false;
    }

    std::fwprintf(stdout, L"[DeFlux_Launcher] Intro skip: using content dir\n  %s\n",
                  content.wstring().c_str());

    bool ok = true;
    ok = backup_and_replace(content / L"Movies" / L"1080" / L"GTA_SA_RSTAR_STINGER_FINAL_1920x1080.mp4",
                            kBlankMp4,
                            sizeof(kBlankMp4),
                            L"Rockstar stinger")
        && ok;
    ok = backup_and_replace(content / L"Splash" / L"Splash.bmp",
                            kBlackBmp,
                            sizeof(kBlackBmp),
                            L"UE Splash.bmp")
        && ok;
    return ok;
}

namespace {

std::wstring user_config_dir() {
    wchar_t* docs = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) || docs == nullptr) {
        return {};
    }
    std::wstring path(docs);
    CoTaskMemFree(docs);
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
        path.push_back(L'\\');
    }
    path += L"Rockstar Games\\GTA San Andreas Definitive Edition\\Config\\WindowsNoEditor\\";
    return path;
}

bool upsert_ini_key(const std::wstring& path,
                    const std::string& section,
                    const std::string& key,
                    const std::string& value) {
    std::ifstream in(path);
    std::stringstream ss;
    if (in) {
        ss << in.rdbuf();
    }
    std::string content = ss.str();
    if (content.empty()) {
        content = "; DeFlux freeroam overrides\n[" + section + "]\n" + key + "=" + value + "\n";
    } else {
        const std::string section_hdr = "[" + section + "]";
        const std::string key_eq = key + "=";
        auto section_pos = content.find(section_hdr);
        if (section_pos == std::string::npos) {
            if (!content.empty() && content.back() != '\n') {
                content.push_back('\n');
            }
            content += section_hdr + "\n" + key_eq + value + "\n";
        } else {
            auto line_start = content.find(key_eq, section_pos);
            auto next_section = content.find("\n[", section_pos + section_hdr.size());
            if (line_start != std::string::npos
                && (next_section == std::string::npos || line_start < next_section)) {
                auto line_end = content.find('\n', line_start);
                if (line_end == std::string::npos) {
                    content.replace(line_start, content.size() - line_start, key_eq + value);
                } else {
                    content.replace(line_start, line_end - line_start, key_eq + value);
                }
            } else {
                auto insert_at = section_pos + section_hdr.size();
                if (insert_at < content.size() && content[insert_at] == '\r') {
                    ++insert_at;
                }
                if (insert_at < content.size() && content[insert_at] == '\n') {
                    ++insert_at;
                }
                content.insert(insert_at, key_eq + value + "\n");
            }
        }
    }

    std::ofstream out(path, std::ios::trunc | std::ios::binary);
    if (!out) {
        return false;
    }
    out << content;
    return true;
}

bool patch_traffic_zero(const std::wstring& path) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    const std::string needle = "Traffic=";
    auto pos = content.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    pos += needle.size();
    auto end = pos;
    while (end < content.size() && content[end] >= '0' && content[end] <= '9') {
        ++end;
    }
    content.replace(pos, end - pos, "0");
    std::ofstream out(path, std::ios::trunc | std::ios::binary);
    if (!out) {
        return false;
    }
    out << content;
    return true;
}

} // namespace

bool apply_freeroam_empty_world_settings() {
    const std::wstring dir = user_config_dir();
    if (dir.empty()) {
        std::fwprintf(stderr, L"[DeFlux_Launcher] Freeroam: Documents config path unavailable.\n");
        return false;
    }

    CreateDirectoryW((dir).c_str(), nullptr); // may already exist; ignore errors via parents
    // Ensure WindowsNoEditor exists (CreateDirectory only one level — best-effort).
    {
        std::wstring build = dir;
        // Create nested dirs via filesystem
        std::error_code ec;
        fs::create_directories(fs::path(dir), ec);
    }

    const std::wstring custom = dir + L"CustomSettings.ini";
    const std::wstring gus = dir + L"GameUserSettings.ini";

    bool ok = true;
    ok = upsert_ini_key(custom, "CustomSettings", "gta.peds.density", "0.0") && ok;
    ok = upsert_ini_key(custom, "CustomSettings", "gta.cars.density", "0.0") && ok;
    ok = upsert_ini_key(custom, "CustomSettings", "gta.peds.distance", "0.0") && ok;
    ok = upsert_ini_key(custom, "CustomSettings", "gta.cars.distance", "0.0") && ok;
    const bool traffic_ok = patch_traffic_zero(gus);

    if (ok) {
        std::fwprintf(stdout,
                      L"[DeFlux_Launcher] Freeroam: zeroed ped/car density in CustomSettings.ini.\n");
    } else {
        std::fwprintf(stderr, L"[DeFlux_Launcher] Freeroam: failed to write CustomSettings.ini.\n");
    }
    if (traffic_ok) {
        std::fwprintf(stdout, L"[DeFlux_Launcher] Freeroam: GameUserSettings Traffic=0.\n");
    } else {
        std::fwprintf(stderr,
                      L"[DeFlux_Launcher] Freeroam: Traffic patch skipped (file missing or unreadable).\n");
    }
    return ok;
}

namespace {

std::wstring profiles_root() {
    wchar_t* docs = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) || docs == nullptr) {
        return {};
    }
    std::wstring path(docs);
    CoTaskMemFree(docs);
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
        path.push_back(L'\\');
    }
    path += L"Rockstar Games\\GTA San Andreas Definitive Edition\\Profiles\\";
    return path;
}

fs::path quarantine_dir_for(const fs::path& profile_dir) {
    return profile_dir / L"deflux_story_backup";
}

fs::path resolve_win64_dir(const std::wstring& hint) {
    fs::path p = hint;
    if (p.empty()) {
        return {};
    }
    for (int i = 0; i < 6 && !p.empty(); ++i) {
        if (fs::exists(p / L"SanAndreas.exe") || fs::exists(p / L"scripts" / L"GTASADE.FusionFix.ini")) {
            return p;
        }
        if (fs::exists(p / L"Gameface" / L"Binaries" / L"Win64" / L"SanAndreas.exe")) {
            return p / L"Gameface" / L"Binaries" / L"Win64";
        }
        if (fs::exists(p / L"Binaries" / L"Win64" / L"SanAndreas.exe")) {
            return p / L"Binaries" / L"Win64";
        }
        p = p.parent_path();
    }
    return {};
}

} // namespace

bool quarantine_story_saves_for_new_game() {
    const std::wstring root = profiles_root();
    if (root.empty() || !fs::exists(root)) {
        std::fwprintf(stdout,
                      L"[DeFlux_Launcher] New Game: no Profiles folder yet (already a fresh start).\n");
        return true;
    }

    std::size_t moved = 0;
    std::error_code ec;
    for (const auto& profile : fs::directory_iterator(root, ec)) {
        if (ec || !profile.is_directory()) {
            continue;
        }
        const fs::path qdir = quarantine_dir_for(profile.path());
        fs::create_directories(qdir, ec);

        for (const auto& file : fs::directory_iterator(profile.path(), ec)) {
            if (ec || !file.is_regular_file()) {
                continue;
            }
            const std::wstring name = file.path().filename().wstring();
            // --new-game only: move story slots + cloud index aside.
            // Never used by clean Slot-8 freeroam mode (that path leaves 1-7 / autosave alone).
            const bool is_slot = name.size() >= 8
                && _wcsnicmp(name.c_str(), L"GTASAsf", 7) == 0
                && _wcsicmp(file.path().extension().c_str(), L".sav") == 0;
            const bool is_cloud = _wcsicmp(name.c_str(), L"cloudsavedata.dat") == 0;
            if (!is_slot && !is_cloud) {
                continue;
            }

            const fs::path dest = qdir / file.path().filename();
            // Don't overwrite an existing quarantine from a crashed previous session.
            if (fs::exists(dest)) {
                fs::remove(file.path(), ec);
            } else {
                fs::rename(file.path(), dest, ec);
                if (ec) {
                    fs::copy_file(file.path(), dest, fs::copy_options::overwrite_existing, ec);
                    fs::remove(file.path(), ec);
                }
            }
            ++moved;
            std::fwprintf(stdout,
                          L"[DeFlux_Launcher] New Game: quarantined %s\n",
                          name.c_str());
        }
    }

    if (moved == 0) {
        std::fwprintf(stdout,
                      L"[DeFlux_Launcher] New Game: no story saves present — SkipMenu will start fresh.\n");
    } else {
        std::fwprintf(stdout,
                      L"[DeFlux_Launcher] New Game: moved %zu save file(s) aside; "
                      L"DeFlux will call StartNewGameFromMenu (no story Resume).\n",
                      static_cast<unsigned long long>(moved));
    }
    return true;
}

bool configure_fusionfix_entry_mode(const std::wstring& win64_or_hint, FusionFixEntryMode mode) {
    try {
        fs::path win64 = resolve_win64_dir(win64_or_hint);
        if (win64.empty()) {
            win64 = fs::path(
                L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\"
                L"GTA San Andreas - The Definitive Edition\\Gameface\\Binaries\\Win64");
        }
        const fs::path ini = win64 / L"scripts" / L"GTASADE.FusionFix.ini";
        if (!fs::exists(ini)) {
            std::fwprintf(stderr,
                          L"[DeFlux_Launcher] FusionFix.ini not found: %s\n",
                          ini.wstring().c_str());
            return false;
        }

        std::ifstream in(ini);
        if (!in) {
            return false;
        }

        const bool skip_menu = (mode != FusionFixEntryMode::NewGame);
        const bool force_freeroam_slot = (mode == FusionFixEntryMode::CleanSave);
        const std::string skip_line = std::string("SkipMenu = ") + (skip_menu ? "1" : "0");
        const std::string slot_line =
            std::string("SaveSlot = ") + std::to_string(kFreeroamSaveSlot);

        std::stringstream out_buf;
        std::string line;
        bool saw_skip = false;
        bool saw_slot = false;
        bool saw_main = false;

        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            std::string key = line;
            while (!key.empty() && (key[0] == ' ' || key[0] == '\t')) {
                key.erase(key.begin());
            }
            if (_strnicmp(key.c_str(), "[MAIN]", 6) == 0) {
                saw_main = true;
                out_buf << line << "\n";
                continue;
            }
            if (_strnicmp(key.c_str(), "SkipMenu", 8) == 0) {
                out_buf << skip_line << "\n";
                saw_skip = true;
                continue;
            }
            if (force_freeroam_slot && _strnicmp(key.c_str(), "SaveSlot", 8) == 0) {
                out_buf << slot_line << "\n";
                saw_slot = true;
                continue;
            }
            out_buf << line << "\n";
        }
        in.close();

        auto insert_under_main = [&](const std::string& addition) {
            std::string text = out_buf.str();
            if (saw_main) {
                const auto pos = text.find("[MAIN]");
                if (pos != std::string::npos) {
                    const auto eol = text.find('\n', pos);
                    if (eol != std::string::npos) {
                        text.insert(eol + 1, addition + "\n");
                    } else {
                        text += "\n" + addition + "\n";
                    }
                } else {
                    text = "[MAIN]\n" + addition + "\n" + text;
                }
            } else {
                text = "[MAIN]\n" + addition + "\n" + text;
            }
            out_buf.str(std::string());
            out_buf.clear();
            out_buf << text;
        };

        if (!saw_skip) {
            insert_under_main(skip_line);
        }
        if (force_freeroam_slot && !saw_slot) {
            insert_under_main(slot_line);
        }

        std::ofstream out(ini, std::ios::trunc);
        if (!out) {
            return false;
        }
        out << out_buf.str();

        if (mode == FusionFixEntryMode::CleanSave) {
            std::wcout << L"[DeFlux_Launcher] FusionFix SkipMenu=1 SaveSlot="
                       << kFreeroamSaveSlot
                       << L" (load GTASAsf8.sav only; slots 1-7 untouched).\n";
        } else if (mode == FusionFixEntryMode::NewGame) {
            std::wcout << L"[DeFlux_Launcher] FusionFix SkipMenu=0 (New Game via DeFlux).\n";
        } else {
            std::wcout << L"[DeFlux_Launcher] FusionFix SkipMenu=1 (resume user SaveSlot).\n";
        }
        return true;
    } catch (const std::exception& ex) {
        std::fwprintf(stderr,
                      L"[DeFlux_Launcher] FusionFix.ini patch failed: %hs\n",
                      ex.what());
        return false;
    } catch (...) {
        std::fwprintf(stderr, L"[DeFlux_Launcher] FusionFix.ini patch failed (unknown).\n");
        return false;
    }
}

bool restore_story_saves_after_session() {
    const std::wstring root = profiles_root();
    if (root.empty() || !fs::exists(root)) {
        return true;
    }

    std::size_t restored = 0;
    std::error_code ec;
    for (const auto& profile : fs::directory_iterator(root, ec)) {
        if (ec || !profile.is_directory()) {
            continue;
        }
        const fs::path qdir = quarantine_dir_for(profile.path());
        if (!fs::exists(qdir)) {
            continue;
        }

        for (const auto& file : fs::directory_iterator(qdir, ec)) {
            if (ec || !file.is_regular_file()) {
                continue;
            }
            const fs::path dest = profile.path() / file.path().filename();
            if (fs::exists(dest)) {
                // Session may have written a freeroam save — keep it, leave story in backup.
                const fs::path alt = dest;
                std::wstring alt_name = dest.filename().wstring() + L".deflux_session";
                fs::rename(dest, dest.parent_path() / alt_name, ec);
                std::fwprintf(stdout,
                              L"[DeFlux_Launcher] New Game: kept session save as %s\n",
                              alt_name.c_str());
            }
            fs::rename(file.path(), dest, ec);
            if (ec) {
                fs::copy_file(file.path(), dest, fs::copy_options::overwrite_existing, ec);
                fs::remove(file.path(), ec);
            }
            ++restored;
            std::fwprintf(stdout,
                          L"[DeFlux_Launcher] New Game: restored story save %s\n",
                          file.path().filename().wstring().c_str());
        }
    }

    if (restored > 0) {
        std::fwprintf(stdout,
                      L"[DeFlux_Launcher] New Game: restored %zu story save(s).\n",
                      static_cast<unsigned long long>(restored));
    }
    return true;
}

} // namespace deflux::launcher
