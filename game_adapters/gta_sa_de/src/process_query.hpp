#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace deflux::game::gta_sa_de::detail {

struct RunningMatch {
    bool running = false;
    std::uint32_t process_id = 0;
    std::wstring image_path;
};

enum class VerifiedQueryStatus {
    NotFound = 0,
    Found,
    UnverifiedCandidate,
    EnumerationFailed,
};

struct VerifiedProcess {
    VerifiedQueryStatus status = VerifiedQueryStatus::NotFound;
    std::uint32_t process_id = 0;
    std::wstring image_path;
    std::uint64_t creation_identity = 0;
    std::string message;
};

// Passive process scan. Opens processes with PROCESS_QUERY_LIMITED_INFORMATION only.
RunningMatch find_running_gta_sa_de(std::wstring_view expected_exe);

// Strict observation: requires a verified install executable path and an exact
// normalized image-path match. Filename-only matches are rejected.
VerifiedProcess query_verified_process(std::wstring_view expected_exe);

bool query_process_identity(std::uint32_t process_id,
                            std::wstring& image_out,
                            std::uint64_t& creation_out);

} // namespace deflux::game::gta_sa_de::detail
