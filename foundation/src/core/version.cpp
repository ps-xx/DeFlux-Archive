#include "deflux/version/version.hpp"

#include <cstdio>

namespace deflux::version {

bool format(const SemanticVersion& version, char* buffer, std::size_t buffer_size) noexcept {
    if (buffer == nullptr || buffer_size == 0) {
        return false;
    }

    const int written = std::snprintf(
        buffer,
        buffer_size,
        "%u.%u.%u",
        static_cast<unsigned>(version.major),
        static_cast<unsigned>(version.minor),
        static_cast<unsigned>(version.patch));

    if (written < 0 || static_cast<std::size_t>(written) >= buffer_size) {
        buffer[0] = '\0';
        return false;
    }

    return true;
}

} // namespace deflux::version
