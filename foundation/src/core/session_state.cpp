#include "deflux/core/session_state.hpp"

namespace deflux::core {

const char* to_string(SessionState state) noexcept {
    switch (state) {
    case SessionState::Disconnected:
        return "DISCONNECTED";
    case SessionState::Connecting:
        return "CONNECTING";
    case SessionState::Handshaking:
        return "HANDSHAKING";
    case SessionState::Connected:
        return "CONNECTED";
    case SessionState::WorldLoading:
        return "WORLD_LOADING";
    case SessionState::Ready:
        return "READY";
    case SessionState::Spawning:
        return "SPAWNING";
    case SessionState::Playing:
        return "PLAYING";
    }

    return "UNKNOWN";
}

} // namespace deflux::core
