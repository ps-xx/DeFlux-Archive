#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace deflux::game::gta_sa_de::detail {

struct FileFingerprint {
    bool ok = false;
    std::string sha256_hex;
    std::uint64_t size_bytes = 0;
    std::string file_version;
    std::string product_version;
};

FileFingerprint fingerprint_executable(std::wstring_view path);

} // namespace deflux::game::gta_sa_de::detail
