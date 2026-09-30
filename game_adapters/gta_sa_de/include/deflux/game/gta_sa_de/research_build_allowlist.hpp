#pragma once

// Task #9B: research-only exact-build allowlist. NOT the production supported registry.

#include <string_view>

namespace deflux::game::gta_sa_de {

inline constexpr const char kResearchAllowlistedSha256[] =
    "63e633bb51c35fe35ca98f503b62e7ddb8403abe4c9d501cfa9162892453846f";

bool is_research_allowlisted_sha256(std::string_view sha256_hex) noexcept;

bool is_production_supported_sha256(std::string_view sha256_hex) noexcept;

} // namespace deflux::game::gta_sa_de
