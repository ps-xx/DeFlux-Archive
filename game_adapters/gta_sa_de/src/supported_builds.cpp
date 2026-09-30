#include "supported_builds.hpp"

#include <array>
#include <cctype>
#include <string>

namespace deflux::game::gta_sa_de::detail {
namespace {

bool is_lowercase_sha256(std::string_view hex) noexcept {
    if (hex.size() != 64) {
        return false;
    }
    for (const char ch : hex) {
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
            return false;
        }
    }
    return true;
}

std::string normalize_sha256(std::string_view hex) {
    if (hex.size() != 64) {
        return {};
    }
    std::string out(hex.size(), '\0');
    for (std::size_t i = 0; i < hex.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(hex[i]);
        if (std::isxdigit(ch) == 0) {
            return {};
        }
        out[i] = static_cast<char>(std::tolower(ch));
    }
    return out;
}

// Explicit allow-list. Empty until a reviewed vanilla SHA-256 is registered.
// Do not guess or copy hashes from unverified machines.
constexpr std::array<std::string_view, 0> kSupportedSha256{};

} // namespace

bool fingerprint_is_registered(std::string_view sha256_hex) noexcept {
    const std::string normalized = normalize_sha256(sha256_hex);
    if (!is_lowercase_sha256(normalized)) {
        return false;
    }

    for (const std::string_view known : kSupportedSha256) {
        if (known == normalized) {
            return true;
        }
    }
    return false;
}

} // namespace deflux::game::gta_sa_de::detail
