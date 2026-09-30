#include "deflux/game/gta_sa_de/research_build_allowlist.hpp"

#include "deflux/game/gta_sa_de/detection.hpp"

#include <array>
#include <cctype>
#include <string>

namespace deflux::game::gta_sa_de {
namespace {

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

constexpr std::array<std::string_view, 1> kResearchAllowlistedSha256Entries = {
    kResearchAllowlistedSha256,
};

} // namespace

bool is_research_allowlisted_sha256(std::string_view sha256_hex) noexcept {
    const std::string normalized = normalize_sha256(sha256_hex);
    if (normalized.size() != 64) {
        return false;
    }
    for (const std::string_view entry : kResearchAllowlistedSha256Entries) {
        if (entry == normalized) {
            return true;
        }
    }
    return false;
}

bool is_production_supported_sha256(std::string_view sha256_hex) noexcept {
    return is_supported_fingerprint(sha256_hex);
}

} // namespace deflux::game::gta_sa_de
