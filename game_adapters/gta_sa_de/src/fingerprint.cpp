#include "fingerprint.hpp"

#include "sha256.hpp"
#include "win_utf8.hpp"

#include <cstdio>
#include <vector>

#pragma comment(lib, "Version.lib")

namespace deflux::game::gta_sa_de::detail {
namespace {

std::string version_quad(DWORD ms, DWORD ls) {
    char text[32]{};
    const int n = std::snprintf(
        text,
        sizeof(text),
        "%u.%u.%u.%u",
        static_cast<unsigned>(HIWORD(ms)),
        static_cast<unsigned>(LOWORD(ms)),
        static_cast<unsigned>(HIWORD(ls)),
        static_cast<unsigned>(LOWORD(ls)));
    if (n <= 0) {
        return {};
    }
    return std::string(text, static_cast<std::size_t>(n));
}

void read_pe_versions(std::wstring_view path, std::string& file_version, std::string& product_version) {
    file_version.clear();
    product_version.clear();

    const std::wstring wide(path);
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(wide.c_str(), &handle);
    if (size == 0) {
        return;
    }

    std::vector<std::uint8_t> block(size);
    if (!GetFileVersionInfoW(wide.c_str(), 0, size, block.data())) {
        return;
    }

    VS_FIXEDFILEINFO* info = nullptr;
    UINT info_size = 0;
    if (!VerQueryValueW(block.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &info_size)
        || info == nullptr
        || info_size < sizeof(VS_FIXEDFILEINFO)
        || info->dwSignature != 0xFEEF04BD) {
        return;
    }

    file_version = version_quad(info->dwFileVersionMS, info->dwFileVersionLS);
    product_version = version_quad(info->dwProductVersionMS, info->dwProductVersionLS);
}

} // namespace

FileFingerprint fingerprint_executable(std::wstring_view path) {
    FileFingerprint result{};
    if (path.empty()) {
        return result;
    }

    if (!sha256_file(path, result.sha256_hex, result.size_bytes)) {
        result.sha256_hex.clear();
        result.size_bytes = 0;
        return result;
    }

    read_pe_versions(path, result.file_version, result.product_version);
    result.ok = true;
    return result;
}

} // namespace deflux::game::gta_sa_de::detail
