#include "deflux/game/adapter.hpp"

namespace deflux::game {

const char* to_string(ProbeStatus status) noexcept {
    switch (status) {
    case ProbeStatus::NotImplemented:
        return "NOT_IMPLEMENTED";
    case ProbeStatus::Unavailable:
        return "UNAVAILABLE";
    case ProbeStatus::Unsupported:
        return "UNSUPPORTED";
    case ProbeStatus::Supported:
        return "SUPPORTED";
    case ProbeStatus::Failed:
        return "FAILED";
    }

    return "UNKNOWN";
}

const char* to_string(Readiness status) noexcept {
    switch (status) {
    case Readiness::NotImplemented:
        return "NOT_IMPLEMENTED";
    case Readiness::Unavailable:
        return "UNAVAILABLE";
    case Readiness::NotReady:
        return "NOT_READY";
    case Readiness::Ready:
        return "READY";
    }

    return "UNKNOWN";
}

} // namespace deflux::game
