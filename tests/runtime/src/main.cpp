#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "deflux/runtime/abi.h"
#include "deflux/runtime/research_harness.h"

#include <cstdio>
#include <cstring>
#include <string>

/*
 * Runtime ABI harness.
 *
 * LoadLibraryW / GetProcAddress / FreeLibrary load DeFlux_Runtime.dll into
 * THIS process only. This is not GTA injection: no remote process, no
 * CreateRemoteThread, no WriteProcessMemory, no ASI/version.dll proxy.
 */

namespace {

int g_failed = 0;
int g_passed = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    if (condition) {
        ++g_passed;
        return;
    }
    ++g_failed;
    std::printf("FAIL %s:%d: %s\n", file, line, expression);
}

#define CHECK(expr) check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

using FnQueryAbi = DefluxRuntimeResult (*)(DefluxRuntimeAbiInfo*);
using FnInitialize = DefluxRuntimeResult (*)(const DefluxRuntimeInitContext*);
using FnShutdown = DefluxRuntimeResult (*)(void);
using FnGetStatus = DefluxRuntimeResult (*)(DefluxRuntimeStatus*);
using FnResultString = const char* (*)(DefluxRuntimeResult);
using FnStateString = const char* (*)(DefluxRuntimeHostState);
using FnResearchSubstate = int32_t (*)(void);
using FnResearchIdentity = uintptr_t (*)(void);
using FnResearchHarnessTest = DefluxRuntimeResult (*)(void);
using FnResearchBootstrapComplete = int32_t (*)(void);

struct Abi {
    FnQueryAbi query_abi = nullptr;
    FnInitialize initialize = nullptr;
    FnShutdown shutdown = nullptr;
    FnGetStatus get_status = nullptr;
    FnResultString result_string = nullptr;
    FnStateString state_string = nullptr;
    FnResearchSubstate research_substate = nullptr;
    FnResearchIdentity research_identity = nullptr;
    FnResearchHarnessTest research_async_shutdown_test = nullptr;
    FnResearchHarnessTest research_async_failure_shutdown_test = nullptr;
    FnResearchBootstrapComplete research_bootstrap_complete = nullptr;
    FnResearchHarnessTest research_auto_stop_stress_test = nullptr;
    FnResearchHarnessTest research_async_shutdown_stress_test = nullptr;
    FnResearchHarnessTest research_async_failure_stress_test = nullptr;
    FnResearchHarnessTest research_lifecycle_breadcrumb_smoke = nullptr;
};

DefluxRuntimeInitContext make_init(uint32_t abi_version, uint32_t flags, uint32_t size) {
    DefluxRuntimeInitContext context{};
    context.struct_size = size;
    context.abi_version = abi_version;
    context.flags = flags;
    return context;
}

DefluxRuntimeInitContext make_valid_init() {
    return make_init(DEFLUX_RUNTIME_ABI_VERSION,
                     DEFLUX_RUNTIME_INIT_FLAG_NONE,
                     static_cast<uint32_t>(sizeof(DefluxRuntimeInitContext)));
}

DefluxRuntimeStatus make_status() {
    DefluxRuntimeStatus status{};
    status.struct_size = static_cast<uint32_t>(sizeof(DefluxRuntimeStatus));
    return status;
}

bool status_is(const Abi& abi, DefluxRuntimeHostState state, DefluxRuntimeResult last) {
    DefluxRuntimeStatus status = make_status();
    if (abi.get_status(&status) != DEFLUX_RUNTIME_OK) {
        return false;
    }
    return status.host_state == state && status.last_result == last;
}

void wait_for_bootstrap(const Abi& abi, DWORD timeout_ms = 5000) {
    if (abi.research_bootstrap_complete == nullptr) {
        return;
    }
    const DWORD deadline = GetTickCount() + timeout_ms;
    while (GetTickCount() < deadline) {
        if (abi.research_bootstrap_complete() != 0) {
            return;
        }
        Sleep(10);
    }
}

std::wstring sibling_dll_path() {
    wchar_t exe[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }

    wchar_t* slash = exe;
    for (wchar_t* p = exe; *p != L'\0'; ++p) {
        if (*p == L'\\' || *p == L'/') {
            slash = p;
        }
    }
    const std::wstring dir(exe, static_cast<std::size_t>(slash - exe + 1));
    return dir + L"DeFlux_Runtime.dll";
}

FARPROC require_export(HMODULE module, const char* name) {
    FARPROC proc = GetProcAddress(module, name);
    CHECK(proc != nullptr);
    if (proc == nullptr) {
        std::printf("missing export: %s\n", name);
    }
    return proc;
}

bool load_abi(HMODULE module, Abi& abi) {
    abi.query_abi = reinterpret_cast<FnQueryAbi>(require_export(module, "deflux_runtime_query_abi"));
    abi.initialize = reinterpret_cast<FnInitialize>(require_export(module, "deflux_runtime_initialize"));
    abi.shutdown = reinterpret_cast<FnShutdown>(require_export(module, "deflux_runtime_shutdown"));
    abi.get_status = reinterpret_cast<FnGetStatus>(require_export(module, "deflux_runtime_get_status"));
    abi.result_string = reinterpret_cast<FnResultString>(require_export(module, "deflux_runtime_result_string"));
    abi.state_string = reinterpret_cast<FnStateString>(require_export(module, "deflux_runtime_state_string"));
    abi.research_substate =
        reinterpret_cast<FnResearchSubstate>(require_export(module, "deflux_runtime_research_substate"));
    abi.research_identity = reinterpret_cast<FnResearchIdentity>(
        require_export(module, "deflux_runtime_research_controller_identity"));
    abi.research_async_shutdown_test = reinterpret_cast<FnResearchHarnessTest>(
        require_export(module, "deflux_runtime_research_harness_async_shutdown_test"));
    abi.research_async_failure_shutdown_test = reinterpret_cast<FnResearchHarnessTest>(
        require_export(module, "deflux_runtime_research_harness_async_failure_shutdown_test"));
    abi.research_bootstrap_complete = reinterpret_cast<FnResearchBootstrapComplete>(
        require_export(module, "deflux_runtime_research_bootstrap_complete"));
    abi.research_auto_stop_stress_test = reinterpret_cast<FnResearchHarnessTest>(
        require_export(module, "deflux_runtime_research_harness_auto_stop_stress_test"));
    abi.research_async_shutdown_stress_test = reinterpret_cast<FnResearchHarnessTest>(
        require_export(module, "deflux_runtime_research_harness_async_shutdown_stress_test"));
    abi.research_async_failure_stress_test = reinterpret_cast<FnResearchHarnessTest>(
        require_export(module, "deflux_runtime_research_harness_async_failure_stress_test"));
    abi.research_lifecycle_breadcrumb_smoke = reinterpret_cast<FnResearchHarnessTest>(
        require_export(module, "deflux_runtime_research_harness_lifecycle_breadcrumb_smoke"));
    return abi.query_abi && abi.initialize && abi.shutdown && abi.get_status && abi.result_string
        && abi.state_string && abi.research_substate && abi.research_identity
        && abi.research_async_shutdown_test && abi.research_async_failure_shutdown_test
        && abi.research_bootstrap_complete && abi.research_auto_stop_stress_test
        && abi.research_async_shutdown_stress_test && abi.research_async_failure_stress_test
        && abi.research_lifecycle_breadcrumb_smoke;
}

void test_sizes_and_version_identity() {
    CHECK(DEFLUX_RUNTIME_ABI_VERSION == 1u);
    CHECK(sizeof(DefluxRuntimeInitContext) == 12);
    CHECK(sizeof(DefluxRuntimeAbiInfo) == 16);
    CHECK(sizeof(DefluxRuntimeStatus) == 272);
    CHECK(DEFLUX_RUNTIME_DIAGNOSTIC_CAPACITY == 256u);
}

void test_strings(const Abi& abi) {
    CHECK(std::strcmp(abi.result_string(DEFLUX_RUNTIME_OK), "OK") == 0);
    CHECK(std::strcmp(abi.result_string(DEFLUX_RUNTIME_INVALID_ARGUMENT), "INVALID_ARGUMENT") == 0);
    CHECK(std::strcmp(abi.result_string(DEFLUX_RUNTIME_ABI_MISMATCH), "ABI_MISMATCH") == 0);
    CHECK(std::strcmp(abi.result_string(DEFLUX_RUNTIME_INVALID_STATE), "INVALID_STATE") == 0);
    CHECK(std::strcmp(abi.result_string(DEFLUX_RUNTIME_INITIALIZATION_FAILED),
                      "INITIALIZATION_FAILED")
          == 0);
    CHECK(std::strcmp(abi.result_string(99), "UNKNOWN") == 0);

    CHECK(std::strcmp(abi.state_string(DEFLUX_RUNTIME_STATE_UNINITIALIZED), "UNINITIALIZED") == 0);
    CHECK(std::strcmp(abi.state_string(DEFLUX_RUNTIME_STATE_INITIALIZING), "INITIALIZING") == 0);
    CHECK(std::strcmp(abi.state_string(DEFLUX_RUNTIME_STATE_INITIALIZED), "INITIALIZED") == 0);
    CHECK(std::strcmp(abi.state_string(DEFLUX_RUNTIME_STATE_SHUTTING_DOWN), "SHUTTING_DOWN") == 0);
    CHECK(std::strcmp(abi.state_string(DEFLUX_RUNTIME_STATE_STOPPED), "STOPPED") == 0);
    CHECK(std::strcmp(abi.state_string(DEFLUX_RUNTIME_STATE_FAILED), "FAILED") == 0);
    CHECK(std::strcmp(abi.state_string(99), "UNKNOWN") == 0);
}

void test_query_abi(const Abi& abi) {
    CHECK(abi.query_abi(nullptr) == DEFLUX_RUNTIME_INVALID_ARGUMENT);

    DefluxRuntimeAbiInfo bad{};
    bad.struct_size = 1;
    CHECK(abi.query_abi(&bad) == DEFLUX_RUNTIME_INVALID_ARGUMENT);

    DefluxRuntimeAbiInfo info{};
    info.struct_size = static_cast<uint32_t>(sizeof(DefluxRuntimeAbiInfo));
    CHECK(abi.query_abi(&info) == DEFLUX_RUNTIME_OK);
    CHECK(info.abi_version == DEFLUX_RUNTIME_ABI_VERSION);
    CHECK(info.struct_size == sizeof(DefluxRuntimeAbiInfo));
    CHECK(info.product_major == 0);
    CHECK(info.product_minor == 1);
    CHECK(info.product_patch == 0);
    CHECK(info.reserved == 0);
}

void test_invalid_initialize(const Abi& abi) {
    CHECK(abi.initialize(nullptr) == DEFLUX_RUNTIME_INVALID_ARGUMENT);
    CHECK(status_is(abi, DEFLUX_RUNTIME_STATE_UNINITIALIZED, DEFLUX_RUNTIME_INVALID_ARGUMENT));

    auto wrong_size = make_init(DEFLUX_RUNTIME_ABI_VERSION, 0, 1);
    CHECK(abi.initialize(&wrong_size) == DEFLUX_RUNTIME_INVALID_ARGUMENT);

    auto mismatch = make_init(DEFLUX_RUNTIME_ABI_VERSION + 1, 0,
                              static_cast<uint32_t>(sizeof(DefluxRuntimeInitContext)));
    CHECK(abi.initialize(&mismatch) == DEFLUX_RUNTIME_ABI_MISMATCH);
    CHECK(status_is(abi, DEFLUX_RUNTIME_STATE_UNINITIALIZED, DEFLUX_RUNTIME_ABI_MISMATCH));

    auto flags = make_valid_init();
    flags.flags = 1;
    CHECK(abi.initialize(&flags) == DEFLUX_RUNTIME_INVALID_ARGUMENT);
    CHECK(status_is(abi, DEFLUX_RUNTIME_STATE_UNINITIALIZED, DEFLUX_RUNTIME_INVALID_ARGUMENT));
}

void test_lifecycle(const Abi& abi) {
    CHECK(abi.shutdown() == DEFLUX_RUNTIME_INVALID_STATE);
    CHECK(status_is(abi, DEFLUX_RUNTIME_STATE_UNINITIALIZED, DEFLUX_RUNTIME_INVALID_STATE));

    const auto context = make_valid_init();
    CHECK(abi.initialize(&context) == DEFLUX_RUNTIME_OK);
    CHECK(status_is(abi, DEFLUX_RUNTIME_STATE_INITIALIZED, DEFLUX_RUNTIME_OK));

    CHECK(abi.initialize(&context) == DEFLUX_RUNTIME_INVALID_STATE);
    CHECK(status_is(abi, DEFLUX_RUNTIME_STATE_INITIALIZED, DEFLUX_RUNTIME_INVALID_STATE));

    CHECK(abi.shutdown() == DEFLUX_RUNTIME_OK);
    CHECK(status_is(abi, DEFLUX_RUNTIME_STATE_STOPPED, DEFLUX_RUNTIME_OK));

    CHECK(abi.shutdown() == DEFLUX_RUNTIME_INVALID_STATE);
    CHECK(status_is(abi, DEFLUX_RUNTIME_STATE_STOPPED, DEFLUX_RUNTIME_INVALID_STATE));

    CHECK(abi.initialize(&context) == DEFLUX_RUNTIME_OK);
    CHECK(status_is(abi, DEFLUX_RUNTIME_STATE_INITIALIZED, DEFLUX_RUNTIME_OK));
    CHECK(abi.shutdown() == DEFLUX_RUNTIME_OK);
    CHECK(status_is(abi, DEFLUX_RUNTIME_STATE_STOPPED, DEFLUX_RUNTIME_OK));
}

void test_research_lifecycle_stress(const Abi& abi) {
    CHECK(abi.research_auto_stop_stress_test() == DEFLUX_RUNTIME_OK);
    CHECK(abi.research_async_shutdown_stress_test() == DEFLUX_RUNTIME_OK);
    CHECK(abi.research_async_failure_stress_test() == DEFLUX_RUNTIME_OK);
}

void test_lifecycle_breadcrumb_smoke(const Abi& abi) {
    CHECK(abi.research_lifecycle_breadcrumb_smoke() == DEFLUX_RUNTIME_OK);
}

void test_research_module_boundary(const Abi& abi) {
    CHECK(abi.research_substate() == 0);

    const uintptr_t identity_before = abi.research_identity();
    CHECK(identity_before != 0);

    const auto context = make_valid_init();
    CHECK(abi.initialize(&context) == DEFLUX_RUNTIME_OK);

    CHECK(abi.research_identity() == identity_before);
    CHECK(abi.research_async_shutdown_test() == DEFLUX_RUNTIME_OK);
    CHECK(abi.research_substate() == 0);
    CHECK(abi.research_identity() == identity_before);

    CHECK(abi.research_async_failure_shutdown_test() == DEFLUX_RUNTIME_OK);
    CHECK(abi.research_substate() == 5);
    CHECK(abi.research_identity() == identity_before);

    CHECK(abi.shutdown() == DEFLUX_RUNTIME_OK);
    CHECK(abi.research_substate() == 5);
    CHECK(abi.research_identity() == identity_before);
}

void test_get_status_args(const Abi& abi) {
    CHECK(abi.get_status(nullptr) == DEFLUX_RUNTIME_INVALID_ARGUMENT);
    DefluxRuntimeStatus bad{};
    bad.struct_size = 4;
    CHECK(abi.get_status(&bad) == DEFLUX_RUNTIME_INVALID_ARGUMENT);
}

int run_with_module(HMODULE module) {
    Abi abi{};
    if (!load_abi(module, abi)) {
        return 1;
    }

    test_sizes_and_version_identity();
    test_strings(abi);
    test_query_abi(abi);
    test_get_status_args(abi);
    test_invalid_initialize(abi);
    test_lifecycle(abi);
    test_research_module_boundary(abi);
    test_research_lifecycle_stress(abi);
    test_lifecycle_breadcrumb_smoke(abi);
    return 0;
}

} // namespace

int main() {
    std::printf("DeFlux Runtime ABI harness\n");
    std::printf("Loads DeFlux_Runtime.dll into this process only (pid %lu).\n",
                static_cast<unsigned long>(GetCurrentProcessId()));

    const std::wstring path = sibling_dll_path();
    if (path.empty()) {
        std::printf("FAIL: could not resolve harness executable path.\n");
        return 1;
    }
    std::printf("DLL: %ls\n", path.c_str());

    HMODULE module = LoadLibraryW(path.c_str());
    CHECK(module != nullptr);
    if (module == nullptr) {
        std::printf("FAIL: LoadLibraryW failed (%lu).\n", static_cast<unsigned long>(GetLastError()));
        std::printf("%d passed, %d failed\n", g_passed, g_failed);
        return 1;
    }

    run_with_module(module);

    {
        Abi abi{};
        if (load_abi(module, abi)) {
            wait_for_bootstrap(abi);
            (void)abi.shutdown();
        }
    }
    CHECK(FreeLibrary(module) != FALSE);
    CHECK(GetModuleHandleW(L"DeFlux_Runtime.dll") == nullptr);

    module = LoadLibraryW(path.c_str());
    CHECK(module != nullptr);
    if (module != nullptr) {
        Abi abi{};
        if (load_abi(module, abi)) {
            wait_for_bootstrap(abi);
            // Deferred DllMain bootstrap initializes the host when the DLL is loaded.
            CHECK(status_is(abi, DEFLUX_RUNTIME_STATE_INITIALIZED, DEFLUX_RUNTIME_OK));
            CHECK(abi.shutdown() == DEFLUX_RUNTIME_OK);
        }
        CHECK(FreeLibrary(module) != FALSE);
        CHECK(GetModuleHandleW(L"DeFlux_Runtime.dll") == nullptr);
    }

    std::printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
