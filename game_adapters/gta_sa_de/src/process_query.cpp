#include "process_query.hpp"

#include "discovery.hpp"
#include "win_utf8.hpp"

#include <TlHelp32.h>

namespace deflux::game::gta_sa_de::detail {
namespace {

struct ScopedHandle {
    HANDLE handle = nullptr;
    ~ScopedHandle() {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};

std::wstring query_image_path(DWORD pid) {
    ScopedHandle process;
    process.handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process.handle == nullptr) {
        return {};
    }

    wchar_t image[32768]{};
    DWORD size = 32768;
    if (!QueryFullProcessImageNameW(process.handle, 0, image, &size) || size == 0) {
        return {};
    }
    const std::wstring raw(image, size);
    const std::wstring canonical = canonicalize_file_path(raw);
    return canonical.empty() ? normalize_path_for_compare(raw) : canonical;
}

bool query_identity(DWORD pid, std::wstring& image_out, std::uint64_t& creation_out) {
    image_out.clear();
    creation_out = 0;

    ScopedHandle process;
    process.handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process.handle == nullptr) {
        return false;
    }

    wchar_t image[32768]{};
    DWORD size = 32768;
    if (!QueryFullProcessImageNameW(process.handle, 0, image, &size) || size == 0) {
        return false;
    }

    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetProcessTimes(process.handle, &created, &exited, &kernel, &user)) {
        return false;
    }

    ULARGE_INTEGER ticks{};
    ticks.LowPart = created.dwLowDateTime;
    ticks.HighPart = created.dwHighDateTime;
    if (ticks.QuadPart == 0) {
        return false;
    }

    const std::wstring raw(image, size);
    const std::wstring canonical = canonicalize_file_path(raw);
    image_out = canonical.empty() ? normalize_path_for_compare(raw) : canonical;
    creation_out = ticks.QuadPart;
    return !image_out.empty();
}

} // namespace

RunningMatch find_running_gta_sa_de(std::wstring_view expected_exe) {
    RunningMatch result{};

    ScopedHandle snap;
    snap.handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap.handle == INVALID_HANDLE_VALUE) {
        return result;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snap.handle, &entry)) {
        return result;
    }

    const std::wstring expected(expected_exe);
    do {
        if (!is_gta_sa_de_executable_name(entry.szExeFile)) {
            continue;
        }

        const std::wstring image = query_image_path(entry.th32ProcessID);
        if (image.empty()) {
            continue;
        }

        const bool path_match = !expected.empty() && executable_paths_equal(image, expected);
        const std::wstring expected_dir = [&]() {
            const auto slash = expected.find_last_of(L"\\/");
            return slash == std::wstring::npos ? std::wstring{} : expected.substr(0, slash);
        }();
        const std::wstring image_dir = [&]() {
            const auto slash = image.find_last_of(L"\\/");
            return slash == std::wstring::npos ? std::wstring{} : image.substr(0, slash);
        }();
        const bool sibling_match = !expected_dir.empty()
            && equals_ignore_case(expected_dir, image_dir)
            && is_gta_sa_de_executable_name(entry.szExeFile);
        const bool layout_match = expected.empty() && looks_like_gta_sa_de_layout(image);
        if (!path_match && !sibling_match && !layout_match) {
            continue;
        }

        result.running = true;
        result.process_id = static_cast<std::uint32_t>(entry.th32ProcessID);
        result.image_path = image;
        return result;
    } while (Process32NextW(snap.handle, &entry));

    return result;
}

VerifiedProcess query_verified_process(std::wstring_view expected_exe) {
    VerifiedProcess result{};

    if (expected_exe.empty()) {
        result.status = VerifiedQueryStatus::NotFound;
        result.message = "No verified installation executable; cannot match a running process.";
        return result;
    }

    ScopedHandle snap;
    snap.handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap.handle == INVALID_HANDLE_VALUE) {
        result.status = VerifiedQueryStatus::EnumerationFailed;
        result.message = "CreateToolhelp32Snapshot failed.";
        return result;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snap.handle, &entry)) {
        result.status = VerifiedQueryStatus::EnumerationFailed;
        result.message = "Process32FirstW failed.";
        return result;
    }

    bool saw_unverified = false;
    bool saw_path_mismatch = false;

    do {
        if (!is_gta_sa_de_executable_name(entry.szExeFile)) {
            continue;
        }

        std::wstring image;
        std::uint64_t creation = 0;
        if (!query_identity(entry.th32ProcessID, image, creation)) {
            saw_unverified = true;
            continue;
        }

        if (!executable_paths_equal(image, std::wstring(expected_exe))) {
            saw_path_mismatch = true;
            continue;
        }

        result.status = VerifiedQueryStatus::Found;
        result.process_id = static_cast<std::uint32_t>(entry.th32ProcessID);
        result.image_path = std::move(image);
        result.creation_identity = creation;
        result.message = "Verified process image path matches the installation executable.";
        return result;
    } while (Process32NextW(snap.handle, &entry));

    if (saw_unverified) {
        result.status = VerifiedQueryStatus::UnverifiedCandidate;
        result.message =
            "A candidate process could not be safely verified (image path or creation time unavailable).";
        return result;
    }

    result.status = VerifiedQueryStatus::NotFound;
    result.message = saw_path_mismatch
        ? "A process with a matching filename was rejected because its image path did not match the installation executable."
        : "No matching process is running.";
    return result;
}

bool query_process_identity(std::uint32_t process_id,
                            std::wstring& image_out,
                            std::uint64_t& creation_out) {
    return query_identity(static_cast<DWORD>(process_id), image_out, creation_out);
}

} // namespace deflux::game::gta_sa_de::detail
