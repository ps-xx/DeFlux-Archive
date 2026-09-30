#include "host.hpp"

#include "in_process_research_host.hpp"
#include "deflux/version/version.hpp"

#include <cstring>
#include <mutex>

namespace deflux::runtime {
namespace {

constexpr std::size_t kDiagnosticCapacity = DEFLUX_RUNTIME_DIAGNOSTIC_CAPACITY;

static_assert(deflux::version::kRuntimeAbi == DEFLUX_RUNTIME_ABI_VERSION,
              "foundation kRuntimeAbi must match the C ABI version");

struct Host {
    std::mutex mutex;
    DefluxRuntimeHostState state = DEFLUX_RUNTIME_STATE_UNINITIALIZED;
    DefluxRuntimeResult last_result = DEFLUX_RUNTIME_OK;
    char diagnostic[kDiagnosticCapacity]{};
};

Host& host() {
    static Host instance;
    return instance;
}

void set_diagnostic(Host& h, const char* text) noexcept {
    std::memset(h.diagnostic, 0, kDiagnosticCapacity);
    if (text == nullptr) {
        return;
    }
    const std::size_t n = std::strlen(text);
    const std::size_t copy = n < (kDiagnosticCapacity - 1) ? n : (kDiagnosticCapacity - 1);
    std::memcpy(h.diagnostic, text, copy);
}

void fill_status_locked(const Host& h, DefluxRuntimeStatus* out_status) noexcept {
    out_status->struct_size = static_cast<uint32_t>(sizeof(DefluxRuntimeStatus));
    out_status->abi_version = DEFLUX_RUNTIME_ABI_VERSION;
    out_status->host_state = h.state;
    out_status->last_result = h.last_result;
    std::memcpy(out_status->diagnostic, h.diagnostic, kDiagnosticCapacity);
    out_status->diagnostic[kDiagnosticCapacity - 1] = '\0';
}

bool size_matches(uint32_t caller_size, std::size_t expected) noexcept {
    return caller_size == static_cast<uint32_t>(expected);
}

} // namespace

DefluxRuntimeResult query_abi(DefluxRuntimeAbiInfo* out_info) noexcept {
    if (out_info == nullptr) {
        return DEFLUX_RUNTIME_INVALID_ARGUMENT;
    }
    if (!size_matches(out_info->struct_size, sizeof(DefluxRuntimeAbiInfo))) {
        return DEFLUX_RUNTIME_INVALID_ARGUMENT;
    }

    out_info->struct_size = static_cast<uint32_t>(sizeof(DefluxRuntimeAbiInfo));
    out_info->abi_version = DEFLUX_RUNTIME_ABI_VERSION;
    out_info->product_major = deflux::version::kApplication.major;
    out_info->product_minor = deflux::version::kApplication.minor;
    out_info->product_patch = deflux::version::kApplication.patch;
    out_info->reserved = 0;
    return DEFLUX_RUNTIME_OK;
}

DefluxRuntimeResult initialize(const DefluxRuntimeInitContext* context) noexcept {
    Host& h = host();
    std::lock_guard<std::mutex> lock(h.mutex);

    if (context == nullptr) {
        h.last_result = DEFLUX_RUNTIME_INVALID_ARGUMENT;
        set_diagnostic(h, "initialize: context is null.");
        return h.last_result;
    }
    if (!size_matches(context->struct_size, sizeof(DefluxRuntimeInitContext))) {
        h.last_result = DEFLUX_RUNTIME_INVALID_ARGUMENT;
        set_diagnostic(h, "initialize: struct_size does not match DefluxRuntimeInitContext.");
        return h.last_result;
    }
    if (context->abi_version != DEFLUX_RUNTIME_ABI_VERSION) {
        h.last_result = DEFLUX_RUNTIME_ABI_MISMATCH;
        set_diagnostic(h, "initialize: bootstrap ABI version does not match the runtime.");
        return h.last_result;
    }
    if (context->flags != DEFLUX_RUNTIME_INIT_FLAG_NONE) {
        h.last_result = DEFLUX_RUNTIME_INVALID_ARGUMENT;
        set_diagnostic(h, "initialize: unknown flags; M1 requires flags=0.");
        return h.last_result;
    }

    if (h.state == DEFLUX_RUNTIME_STATE_INITIALIZED
        || h.state == DEFLUX_RUNTIME_STATE_INITIALIZING
        || h.state == DEFLUX_RUNTIME_STATE_SHUTTING_DOWN) {
        h.last_result = DEFLUX_RUNTIME_INVALID_STATE;
        set_diagnostic(h, "initialize: runtime is already active.");
        return h.last_result;
    }

    h.state = DEFLUX_RUNTIME_STATE_INITIALIZING;
    /* M1: no threads, networking, discovery, GTA attach, or Game Adapter calls. */
    h.state = DEFLUX_RUNTIME_STATE_INITIALIZED;
    h.last_result = DEFLUX_RUNTIME_OK;
    set_diagnostic(h, "initialize: runtime host initialized.");
    return h.last_result;
}

DefluxRuntimeResult shutdown() noexcept {
    Host& h = host();
    std::lock_guard<std::mutex> lock(h.mutex);

    if (h.state != DEFLUX_RUNTIME_STATE_INITIALIZED) {
        h.last_result = DEFLUX_RUNTIME_INVALID_STATE;
        set_diagnostic(h, "shutdown: runtime is not INITIALIZED.");
        return h.last_result;
    }

    h.state = DEFLUX_RUNTIME_STATE_SHUTTING_DOWN;
    deflux::runtime::shutdown_research_from_host();
    h.state = DEFLUX_RUNTIME_STATE_STOPPED;
    h.last_result = DEFLUX_RUNTIME_OK;
    set_diagnostic(h, "shutdown: runtime host stopped.");
    return h.last_result;
}

DefluxRuntimeResult get_status(DefluxRuntimeStatus* out_status) noexcept {
    if (out_status == nullptr) {
        return DEFLUX_RUNTIME_INVALID_ARGUMENT;
    }
    if (!size_matches(out_status->struct_size, sizeof(DefluxRuntimeStatus))) {
        return DEFLUX_RUNTIME_INVALID_ARGUMENT;
    }

    Host& h = host();
    std::lock_guard<std::mutex> lock(h.mutex);
    fill_status_locked(h, out_status);
    return DEFLUX_RUNTIME_OK;
}

} // namespace deflux::runtime
