#include "host.hpp"

extern "C" {

DefluxRuntimeResult deflux_runtime_query_abi(DefluxRuntimeAbiInfo* out_info) {
    try {
        return deflux::runtime::query_abi(out_info);
    } catch (...) {
        return DEFLUX_RUNTIME_INITIALIZATION_FAILED;
    }
}

DefluxRuntimeResult deflux_runtime_initialize(const DefluxRuntimeInitContext* context) {
    try {
        return deflux::runtime::initialize(context);
    } catch (...) {
        return DEFLUX_RUNTIME_INITIALIZATION_FAILED;
    }
}

DefluxRuntimeResult deflux_runtime_shutdown(void) {
    try {
        return deflux::runtime::shutdown();
    } catch (...) {
        return DEFLUX_RUNTIME_INITIALIZATION_FAILED;
    }
}

DefluxRuntimeResult deflux_runtime_get_status(DefluxRuntimeStatus* out_status) {
    try {
        return deflux::runtime::get_status(out_status);
    } catch (...) {
        return DEFLUX_RUNTIME_INITIALIZATION_FAILED;
    }
}

const char* deflux_runtime_result_string(DefluxRuntimeResult result) {
    switch (result) {
    case DEFLUX_RUNTIME_OK:
        return "OK";
    case DEFLUX_RUNTIME_INVALID_ARGUMENT:
        return "INVALID_ARGUMENT";
    case DEFLUX_RUNTIME_ABI_MISMATCH:
        return "ABI_MISMATCH";
    case DEFLUX_RUNTIME_INVALID_STATE:
        return "INVALID_STATE";
    case DEFLUX_RUNTIME_INITIALIZATION_FAILED:
        return "INITIALIZATION_FAILED";
    default:
        return "UNKNOWN";
    }
}

const char* deflux_runtime_state_string(DefluxRuntimeHostState state) {
    switch (state) {
    case DEFLUX_RUNTIME_STATE_UNINITIALIZED:
        return "UNINITIALIZED";
    case DEFLUX_RUNTIME_STATE_INITIALIZING:
        return "INITIALIZING";
    case DEFLUX_RUNTIME_STATE_INITIALIZED:
        return "INITIALIZED";
    case DEFLUX_RUNTIME_STATE_SHUTTING_DOWN:
        return "SHUTTING_DOWN";
    case DEFLUX_RUNTIME_STATE_STOPPED:
        return "STOPPED";
    case DEFLUX_RUNTIME_STATE_FAILED:
        return "FAILED";
    default:
        return "UNKNOWN";
    }
}

} // extern "C"

namespace {
constexpr uint32_t kInitSize = static_cast<uint32_t>(sizeof(DefluxRuntimeInitContext));
constexpr uint32_t kInfoSize = static_cast<uint32_t>(sizeof(DefluxRuntimeAbiInfo));
constexpr uint32_t kStatusSize = static_cast<uint32_t>(sizeof(DefluxRuntimeStatus));
static_assert(kInitSize == 12, "DefluxRuntimeInitContext packed size");
static_assert(kInfoSize == 16, "DefluxRuntimeAbiInfo packed size");
static_assert(kStatusSize == 272, "DefluxRuntimeStatus packed size");
static_assert(DEFLUX_RUNTIME_ABI_VERSION == 1u, "ABI version identity");
} // namespace
