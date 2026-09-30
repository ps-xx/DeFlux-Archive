#include "clean_save_deployer.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <ShlObj.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "Shell32.lib")

namespace fs = std::filesystem;

namespace deflux::launcher {
namespace {

constexpr wchar_t kBackupSuffix[] = L".deflux_user_bak";

std::wstring documents_profiles_root() {
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

std::wstring exe_directory() {
    wchar_t module[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    return fs::path(module).parent_path().wstring();
}

std::wstring slot_filename(int slot) {
    // SADE on-disk name is GTASAsfN.sav (not GTSAslotN.sav).
    return L"GTASAsf" + std::to_wstring(slot) + L".sav";
}

bool read_file_bytes(const fs::path& path, std::vector<char>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::streamoff>(in.tellg());
    if (size <= 0) {
        return false;
    }
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    in.read(out.data(), size);
    return static_cast<bool>(in) || in.eof();
}

bool write_file_bytes(const fs::path& path, const std::vector<char>& data) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

// Copy existing file aside once; leave original path free for overwrite.
bool backup_copy_if_exists(const fs::path& target) {
    if (!fs::exists(target)) {
        return true;
    }
    const fs::path bak = fs::path(target.wstring() + kBackupSuffix);
    if (fs::exists(bak)) {
        return true; // keep first user copy of Slot 8 / cloud for this machine
    }
    std::error_code ec;
    fs::copy_file(target, bak, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::fwprintf(stderr,
                      L"[DeFlux_Launcher] Slot 8: backup failed for %s (%hs)\n",
                      target.filename().wstring().c_str(),
                      ec.message().c_str());
        return false;
    }
    std::fwprintf(stdout,
                  L"[DeFlux_Launcher] Slot 8: backed up %s -> *.deflux_user_bak\n",
                  target.filename().wstring().c_str());
    return true;
}

bool restore_backup_if_present(const fs::path& target) {
    const fs::path bak = fs::path(target.wstring() + kBackupSuffix);
    if (!fs::exists(bak)) {
        return true;
    }
    std::error_code ec;
    fs::copy_file(bak, target, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::fwprintf(stderr,
                      L"[DeFlux_Launcher] Slot 8: restore failed for %s (%hs)\n",
                      target.filename().wstring().c_str(),
                      ec.message().c_str());
        return false;
    }
    fs::remove(bak, ec);
    std::fwprintf(stdout,
                  L"[DeFlux_Launcher] Slot 8: restored user %s\n",
                  target.filename().wstring().c_str());
    return true;
}

// Point resume index at Slot 8 without touching any GTASAsf1..7 / autosave files.
bool point_cloud_index_at_slot8(const fs::path& profile_dir) {
    const fs::path cloud = profile_dir / L"cloudsavedata.dat";
    if (!backup_copy_if_exists(cloud)) {
        return false;
    }

    std::vector<char> bytes;
    if (fs::exists(cloud) && read_file_bytes(cloud, bytes) && bytes.size() >= 8) {
        // Preserve existing blob; only rewrite the slot u32 at offset 4 (LE).
        const auto s = static_cast<std::uint32_t>(kFreeroamSaveSlot);
        bytes[4] = static_cast<char>(s & 0xFF);
        bytes[5] = static_cast<char>((s >> 8) & 0xFF);
        bytes[6] = static_cast<char>((s >> 16) & 0xFF);
        bytes[7] = static_cast<char>((s >> 24) & 0xFF);
    } else {
        bytes.assign(20, 0);
        bytes[0] = 1;
        bytes[4] = static_cast<char>(kFreeroamSaveSlot);
    }
    return write_file_bytes(cloud, bytes);
}

bool deploy_into_profile(const fs::path& profile_dir, const std::vector<char>& clean) {
    const fs::path slot8 = profile_dir / slot_filename(kFreeroamSaveSlot);

    if (!backup_copy_if_exists(slot8)) {
        return false;
    }
    if (!write_file_bytes(slot8, clean)) {
        std::fwprintf(stderr,
                      L"[DeFlux_Launcher] Slot 8: failed to write %s\n",
                      slot8.wstring().c_str());
        return false;
    }

    if (!point_cloud_index_at_slot8(profile_dir)) {
        std::fwprintf(stderr,
                      L"[DeFlux_Launcher] Slot 8: cloudsavedata.dat index update failed "
                      L"(Slot 8 file written; SkipMenu may still resume last story save).\n");
    }

    std::fwprintf(stdout,
                  L"[DeFlux_Launcher] Slot 8: deployed clean freeroam save -> %s\n"
                  L"  (GTSAautosave.sav and GTASAsf1..7 left untouched)\n",
                  slot8.filename().wstring().c_str());
    return true;
}

bool restore_profile(const fs::path& profile_dir) {
    bool ok = true;
    ok = restore_backup_if_present(profile_dir / slot_filename(kFreeroamSaveSlot)) && ok;
    ok = restore_backup_if_present(profile_dir / L"cloudsavedata.dat") && ok;
    return ok;
}

} // namespace

std::wstring find_clean_save_template() {
    const std::wstring exe_dir = exe_directory();
    const std::wstring candidates[] = {
        exe_dir + L"\\DeFlux_clean.sav",
        exe_dir + L"\\assets\\DeFlux_clean.sav",
        L"launcher\\assets\\DeFlux_clean.sav",
        L"assets\\DeFlux_clean.sav",
        L"DeFlux_clean.sav",
    };
    for (const auto& c : candidates) {
        if (!c.empty() && GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return c;
        }
    }

    if (!exe_dir.empty()) {
        fs::path p(exe_dir);
        for (int i = 0; i < 6 && !p.empty(); ++i) {
            const fs::path try1 = p / L"launcher" / L"assets" / L"DeFlux_clean.sav";
            const fs::path try2 = p / L"assets" / L"DeFlux_clean.sav";
            if (fs::exists(try1)) {
                return try1.wstring();
            }
            if (fs::exists(try2)) {
                return try2.wstring();
            }
            if (!p.has_parent_path() || p.parent_path() == p) {
                break;
            }
            p = p.parent_path();
        }
    }
    return {};
}

bool deploy_clean_freeroam_saves() {
    const std::wstring template_path = find_clean_save_template();
    if (template_path.empty()) {
        std::fwprintf(stderr,
                      L"[DeFlux_Launcher] Slot 8: DeFlux_clean.sav not found "
                      L"(expected beside DeFlux_Launcher.exe or in launcher\\assets\\).\n");
        return false;
    }

    std::vector<char> clean;
    if (!read_file_bytes(template_path, clean) || clean.empty()) {
        std::fwprintf(stderr,
                      L"[DeFlux_Launcher] Slot 8: failed to read template:\n  %s\n",
                      template_path.c_str());
        return false;
    }

    std::fwprintf(stdout,
                  L"[DeFlux_Launcher] Slot 8: using template (%zu bytes):\n  %s\n",
                  static_cast<unsigned long long>(clean.size()),
                  template_path.c_str());

    const std::wstring root = documents_profiles_root();
    if (root.empty()) {
        std::fwprintf(stderr, L"[DeFlux_Launcher] Slot 8: Documents path unavailable.\n");
        return false;
    }

    std::error_code ec;
    fs::create_directories(root, ec);
    if (!fs::exists(root)) {
        std::fwprintf(stderr,
                      L"[DeFlux_Launcher] Slot 8: Profiles folder missing:\n  %s\n",
                      root.c_str());
        return false;
    }

    std::size_t profiles = 0;
    bool any_ok = false;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (ec || !entry.is_directory()) {
            continue;
        }
        const std::wstring name = entry.path().filename().wstring();
        if (name.find(L"deflux_") == 0) {
            continue;
        }
        ++profiles;
        any_ok = deploy_into_profile(entry.path(), clean) || any_ok;
    }

    if (profiles == 0) {
        const fs::path fresh = fs::path(root) / L"DeFlux";
        fs::create_directories(fresh, ec);
        any_ok = deploy_into_profile(fresh, clean);
        profiles = 1;
    }

    if (!any_ok) {
        std::fwprintf(stderr, L"[DeFlux_Launcher] Slot 8: deployment failed for all profiles.\n");
        return false;
    }

    std::fwprintf(stdout,
                  L"[DeFlux_Launcher] Slot 8 ready (profiles=%zu). FusionFix SaveSlot=%d / "
                  L"GTASAsf%d.sav only — story slots 1-7 and GTSAautosave.sav untouched.\n",
                  static_cast<unsigned long long>(profiles),
                  kFreeroamSaveSlot,
                  kFreeroamSaveSlot);
    return true;
}

bool restore_clean_freeroam_saves() {
    const std::wstring root = documents_profiles_root();
    if (root.empty() || !fs::exists(root)) {
        return true;
    }

    std::error_code ec;
    bool ok = true;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (ec || !entry.is_directory()) {
            continue;
        }
        ok = restore_profile(entry.path()) && ok;
    }
    return ok;
}

} // namespace deflux::launcher
