#include "remote_read.hpp"

#include "discovery.hpp"

namespace deflux::game::gta_sa_de::detail {

bool ScopedProcess::query_identity(std::uint32_t& process_id,
                                   std::uint64_t& creation_identity,
                                   std::wstring& image_path) const {
    process_id = 0;
    creation_identity = 0;
    image_path.clear();
    if (handle_ == nullptr) {
        return false;
    }

    process_id = GetProcessId(handle_);
    if (process_id == 0) {
        return false;
    }

    wchar_t image[32768]{};
    DWORD size = 32768;
    if (!QueryFullProcessImageNameW(handle_, 0, image, &size) || size == 0) {
        return false;
    }

    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetProcessTimes(handle_, &created, &exited, &kernel, &user)) {
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
    image_path = canonical.empty() ? normalize_path_for_compare(raw) : canonical;
    creation_identity = ticks.QuadPart;
    return !image_path.empty();
}

} // namespace deflux::game::gta_sa_de::detail
