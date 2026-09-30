#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <process.h>

#include <atomic>
#include <exception>

#include "deflux/game/gta_sa_de/research_activation_status.hpp"
#include "deflux/game/gta_sa_de/research_evidence_log.hpp"
#include "deflux/runtime/abi.h"
#include "research_activation.hpp"

namespace {

std::atomic<bool> g_bootstrap_complete{false};

unsigned __stdcall deferred_runtime_bootstrap(void*) {
    using deflux::game::gta_sa_de::research_evidence_append_lifecycle;
    try {
        research_evidence_append_lifecycle("BOOTSTRAP_THREAD_ENTER", "");
        DefluxRuntimeInitContext context{};
        context.struct_size = static_cast<uint32_t>(sizeof(DefluxRuntimeInitContext));
        context.abi_version = DEFLUX_RUNTIME_ABI_VERSION;
        context.flags = DEFLUX_RUNTIME_INIT_FLAG_NONE;
        research_evidence_append_lifecycle("RUNTIME_INITIALIZE_BEGIN", "");
        const DefluxRuntimeResult init_result = deflux_runtime_initialize(&context);
        if (init_result == DEFLUX_RUNTIME_OK) {
            research_evidence_append_lifecycle("RUNTIME_INITIALIZE_OK", "");
        } else {
            research_evidence_append_lifecycle("RUNTIME_INITIALIZE_FAILED", "");
        }
        g_bootstrap_complete.store(true, std::memory_order_release);
        deflux::game::gta_sa_de::research_activation_status_publish_bootstrap_complete();
        deflux::runtime::try_activate_research_from_intent();
        research_evidence_append_lifecycle("BOOTSTRAP_THREAD_EXIT", "");
    } catch (const std::exception& ex) {
        research_evidence_append_lifecycle("BOOTSTRAP_EXCEPTION", ex.what());
    } catch (...) {
        research_evidence_append_lifecycle("BOOTSTRAP_EXCEPTION", "unknown");
    }
    return 0;
}

} // namespace

namespace deflux::runtime {

bool research_bootstrap_complete() noexcept {
    return g_bootstrap_complete.load(std::memory_order_acquire);
}

} // namespace deflux::runtime

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        // Must use _beginthreadex (not CreateThread): deferred bootstrap runs CRT-backed
        // C++ (std::mutex/string/thread). CreateThread skips CRT thread attach and can
        // hang or abort when the sampler std::thread is later started.
        const uintptr_t thread = _beginthreadex(nullptr, 0, deferred_runtime_bootstrap, nullptr, 0, nullptr);
        if (thread != 0) {
            CloseHandle(reinterpret_cast<HANDLE>(thread));
        }
    }
    return TRUE;
}
