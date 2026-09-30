#include "module_query.hpp"

#include "discovery.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <TlHelp32.h>

namespace deflux::game::gta_sa_de::detail {
namespace {

struct ScopedSnap {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~ScopedSnap() {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};

} // namespace

MainModule query_main_module(std::uint32_t process_id, std::wstring_view expected_exe) {
    MainModule result{};
    if (process_id == 0 || expected_exe.empty()) {
        result.message = "Main-module query requires a verified process identity.";
        return result;
    }

    ScopedSnap snap;
    snap.handle = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);
    if (snap.handle == INVALID_HANDLE_VALUE) {
        result.message = "CreateToolhelp32Snapshot(TH32CS_SNAPMODULE) failed.";
        return result;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snap.handle, &entry)) {
        result.message = "Module32FirstW failed.";
        return result;
    }

    const std::wstring expected(expected_exe);
    do {
        const std::wstring raw = entry.szExePath;
        const std::wstring canonical = canonicalize_file_path(raw);
        const std::wstring path = canonical.empty() ? normalize_path_for_compare(raw) : canonical;
        if (!executable_paths_equal(path, expected)) {
            continue;
        }

        const auto base = reinterpret_cast<std::uint64_t>(entry.modBaseAddr);
        const auto size = static_cast<std::uint64_t>(entry.modBaseSize);
        if (base == 0 || size < 0x1000ull || size > 0x40000000ull) {
            result.message = "Main module base or size is not plausible.";
            return result;
        }

        result.found = true;
        result.base = base;
        result.size = size;
        result.path = path;
        result.message = "Main module path matches the verified executable.";
        return result;
    } while (Module32NextW(snap.handle, &entry));

    result.message = "No module image path matched the verified executable.";
    return result;
}

bool query_loaded_module_ranges(std::uint32_t process_id, std::vector<ModuleRange>& out) {
    out.clear();
    if (process_id == 0) {
        return false;
    }

    ScopedSnap snap;
    snap.handle = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);
    if (snap.handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snap.handle, &entry)) {
        return false;
    }

    do {
        const auto base = reinterpret_cast<std::uint64_t>(entry.modBaseAddr);
        const auto size = static_cast<std::uint64_t>(entry.modBaseSize);
        if (base == 0 || size < 0x1000ull || size > 0x40000000ull) {
            continue;
        }
        out.push_back(ModuleRange{base, size});
    } while (Module32NextW(snap.handle, &entry));

    return !out.empty();
}

} // namespace deflux::game::gta_sa_de::detail
