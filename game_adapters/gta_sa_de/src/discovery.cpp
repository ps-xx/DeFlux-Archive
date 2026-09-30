#include "discovery.hpp"

#include "win_utf8.hpp"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "Advapi32.lib")

namespace deflux::game::gta_sa_de::detail {
namespace {

constexpr std::uint32_t kSteamAppId = 1547000;
constexpr wchar_t kDefaultInstallDir[] = L"GTA San Andreas - The Definitive Edition";
constexpr wchar_t kPrimaryExe[] = L"SanAndreas.exe";
constexpr wchar_t kShippingExe[] = L"Gameface-Win64-Shipping.exe";

std::wstring read_reg_sz(HKEY root, const wchar_t* subkey, const wchar_t* value_name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return {};
    }

    wchar_t buffer[32768]{};
    DWORD type = 0;
    DWORD size = sizeof(buffer);
    const LONG status = RegQueryValueExW(
        key, value_name, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);

    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return {};
    }
    return buffer;
}

void add_unique_dir(std::vector<std::wstring>& dirs, std::wstring path) {
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }
    if (path.empty()) {
        return;
    }
    for (const auto& existing : dirs) {
        if (equals_ignore_case(existing, path)) {
            return;
        }
    }
    dirs.push_back(std::move(path));
}

std::vector<std::wstring> parse_library_paths(const std::string& vdf) {
    std::vector<std::wstring> paths;
    std::size_t i = 0;
    while (i < vdf.size()) {
        if (vdf[i] != '"') {
            ++i;
            continue;
        }
        const std::size_t key_begin = i + 1;
        std::size_t key_end = key_begin;
        while (key_end < vdf.size() && vdf[key_end] != '"') {
            ++key_end;
        }
        if (key_end >= vdf.size()) {
            break;
        }
        const std::string key = vdf.substr(key_begin, key_end - key_begin);
        i = key_end + 1;
        while (i < vdf.size() && (vdf[i] == ' ' || vdf[i] == '\t' || vdf[i] == '\r' || vdf[i] == '\n')) {
            ++i;
        }
        if (i >= vdf.size() || vdf[i] != '"') {
            continue;
        }
        ++i;
        std::string value;
        while (i < vdf.size() && vdf[i] != '"') {
            if (vdf[i] == '\\' && i + 1 < vdf.size()) {
                value.push_back(vdf[i + 1]);
                i += 2;
                continue;
            }
            value.push_back(vdf[i]);
            ++i;
        }
        if (key == "path") {
            add_unique_dir(paths, utf8_to_wide(value));
        }
    }
    return paths;
}

std::string read_narrow_file(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::wstring parse_installdir(const std::string& acf) {
    const std::string key = "\"installdir\"";
    const std::size_t pos = acf.find(key);
    if (pos == std::string::npos) {
        return {};
    }
    std::size_t i = pos + key.size();
    while (i < acf.size() && (acf[i] == ' ' || acf[i] == '\t')) {
        ++i;
    }
    if (i >= acf.size() || acf[i] != '"') {
        return {};
    }
    ++i;
    std::string value;
    while (i < acf.size() && acf[i] != '"') {
        if (acf[i] == '\\' && i + 1 < acf.size()) {
            value.push_back(acf[i + 1]);
            i += 2;
            continue;
        }
        value.push_back(acf[i]);
        ++i;
    }
    return utf8_to_wide(value);
}

std::wstring filename_of(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

std::wstring parent_of(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }
    return path.substr(0, slash);
}

bool file_exists(const std::wstring& path) {
    return classify_path(path) == PathKind::File;
}

std::wstring pick_exe_in_win64(const std::wstring& win64) {
    const std::wstring primary = win64 + L"\\" + kPrimaryExe;
    if (file_exists(primary)) {
        return canonicalize_file_path(primary);
    }
    const std::wstring shipping = win64 + L"\\" + kShippingExe;
    if (file_exists(shipping)) {
        return canonicalize_file_path(shipping);
    }
    return {};
}

std::wstring exe_from_library(const std::wstring& library, const std::wstring& installdir) {
    if (library.empty() || installdir.empty()) {
        return {};
    }
    const std::wstring win64 = library + L"\\steamapps\\common\\" + installdir
        + L"\\Gameface\\Binaries\\Win64";
    return pick_exe_in_win64(win64);
}

void collect_steam_libraries(std::vector<std::wstring>& libraries) {
    const std::wstring hkcu_path = read_reg_sz(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    add_unique_dir(libraries, hkcu_path);

    const std::wstring hkcu_exe = read_reg_sz(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamExe");
    if (!hkcu_exe.empty()) {
        add_unique_dir(libraries, parent_of(hkcu_exe));
    }

    const std::wstring hklm = read_reg_sz(
        HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    add_unique_dir(libraries, hklm);

    std::vector<std::wstring> extra;
    for (const auto& root : libraries) {
        const std::wstring vdf = root + L"\\steamapps\\libraryfolders.vdf";
        const std::string text = read_narrow_file(vdf);
        if (text.empty()) {
            continue;
        }
        for (auto& parsed : parse_library_paths(text)) {
            extra.push_back(std::move(parsed));
        }
    }
    for (auto& path : extra) {
        add_unique_dir(libraries, std::move(path));
    }
}

} // namespace

PathKind classify_path(const std::wstring& path) {
    if (path.empty()) {
        return PathKind::Missing;
    }
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return PathKind::Missing;
    }
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return PathKind::NotAFile;
    }
    return PathKind::File;
}

std::wstring normalize_path_for_compare(const std::wstring& path) {
    if (path.empty()) {
        return {};
    }

    std::wstring unified = path;
    for (auto& ch : unified) {
        if (ch == L'/') {
            ch = L'\\';
        }
    }

    wchar_t full[32768]{};
    const DWORD n = GetFullPathNameW(unified.c_str(), 32768, full, nullptr);
    std::wstring resolved = (n == 0 || n >= 32768) ? unified : std::wstring(full);

    wchar_t long_path[32768]{};
    const DWORD long_n = GetLongPathNameW(resolved.c_str(), long_path, 32768);
    if (long_n > 0 && long_n < 32768) {
        resolved.assign(long_path, long_n);
    }
    return resolved;
}

std::wstring canonicalize_file_path(const std::wstring& path) {
    if (classify_path(path) != PathKind::File) {
        return {};
    }
    return normalize_path_for_compare(path);
}

bool executable_paths_equal(const std::wstring& left, const std::wstring& right) {
    const std::wstring a = normalize_path_for_compare(left);
    const std::wstring b = normalize_path_for_compare(right);
    if (a.empty() || b.empty()) {
        return false;
    }
    return equals_ignore_case(a, b);
}

bool is_gta_sa_de_executable_name(std::wstring_view filename) noexcept {
    return equals_ignore_case(filename, kPrimaryExe)
        || equals_ignore_case(filename, kShippingExe);
}

bool looks_like_gta_sa_de_layout(const std::wstring& path) noexcept {
    if (!is_gta_sa_de_executable_name(filename_of(path))) {
        return false;
    }
    const std::wstring win64 = parent_of(path);
    const std::wstring binaries = parent_of(win64);
    const std::wstring gameface = parent_of(binaries);
    const std::wstring win64_name = filename_of(win64);
    const std::wstring binaries_name = filename_of(binaries);
    const std::wstring gameface_name = filename_of(gameface);
    return equals_ignore_case(win64_name, L"Win64")
        && equals_ignore_case(binaries_name, L"Binaries")
        && equals_ignore_case(gameface_name, L"Gameface");
}

std::wstring discover_gta_sa_de_executable() {
    std::vector<std::wstring> libraries;
    collect_steam_libraries(libraries);

    for (const auto& library : libraries) {
        const std::wstring acf =
            library + L"\\steamapps\\appmanifest_" + std::to_wstring(kSteamAppId) + L".acf";
        std::wstring installdir = parse_installdir(read_narrow_file(acf));
        if (installdir.empty()) {
            installdir = kDefaultInstallDir;
        }
        if (const std::wstring exe = exe_from_library(library, installdir); !exe.empty()) {
            return exe;
        }
        if (installdir != kDefaultInstallDir) {
            if (const std::wstring exe = exe_from_library(library, kDefaultInstallDir); !exe.empty()) {
                return exe;
            }
        }
    }

    return {};
}

} // namespace deflux::game::gta_sa_de::detail
