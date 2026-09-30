#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(DEFLUX_RUNTIME_EXPORTS)
#define DEFLUX_RUNTIME_API __declspec(dllexport)
#else
#define DEFLUX_RUNTIME_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Independent of product SemVer, protocol SemVer, Game Adapter API, and GTA SHA-256. */
#define DEFLUX_RUNTIME_ABI_VERSION 1u
#define DEFLUX_RUNTIME_DIAGNOSTIC_CAPACITY 256u
#define DEFLUX_RUNTIME_INIT_FLAG_NONE 0u

typedef int32_t DefluxRuntimeResult;
enum {
    DEFLUX_RUNTIME_OK = 0,
    DEFLUX_RUNTIME_INVALID_ARGUMENT = 1,
    DEFLUX_RUNTIME_ABI_MISMATCH = 2,
    DEFLUX_RUNTIME_INVALID_STATE = 3,
    DEFLUX_RUNTIME_INITIALIZATION_FAILED = 4
};

typedef int32_t DefluxRuntimeHostState;
enum {
    DEFLUX_RUNTIME_STATE_UNINITIALIZED = 0,
    DEFLUX_RUNTIME_STATE_INITIALIZING = 1,
    DEFLUX_RUNTIME_STATE_INITIALIZED = 2,
    DEFLUX_RUNTIME_STATE_SHUTTING_DOWN = 3,
    DEFLUX_RUNTIME_STATE_STOPPED = 4,
    DEFLUX_RUNTIME_STATE_FAILED = 5
};

#pragma pack(push, 1)
typedef struct DefluxRuntimeInitContext {
    uint32_t struct_size; /* must equal sizeof(DefluxRuntimeInitContext) */
    uint32_t abi_version; /* must equal DEFLUX_RUNTIME_ABI_VERSION */
    uint32_t flags;       /* must be DEFLUX_RUNTIME_INIT_FLAG_NONE in M1 */
} DefluxRuntimeInitContext;

typedef struct DefluxRuntimeAbiInfo {
    uint32_t struct_size; /* caller sets expected size; must match */
    uint32_t abi_version;
    uint16_t product_major;
    uint16_t product_minor;
    uint16_t product_patch;
    uint16_t reserved;
} DefluxRuntimeAbiInfo;

typedef struct DefluxRuntimeStatus {
    uint32_t struct_size; /* caller sets expected size; must match */
    uint32_t abi_version;
    int32_t host_state;
    int32_t last_result;
    char diagnostic[DEFLUX_RUNTIME_DIAGNOSTIC_CAPACITY];
} DefluxRuntimeStatus;
#pragma pack(pop)

/*
 * C ABI, __cdecl. No STL, exceptions, or HANDLEs.
 * diagnostic buffers are caller-owned. result_string returns process-lifetime static text.
 */
DEFLUX_RUNTIME_API DefluxRuntimeResult deflux_runtime_query_abi(DefluxRuntimeAbiInfo* out_info);
DEFLUX_RUNTIME_API DefluxRuntimeResult deflux_runtime_initialize(const DefluxRuntimeInitContext* context);
DEFLUX_RUNTIME_API DefluxRuntimeResult deflux_runtime_shutdown(void);
DEFLUX_RUNTIME_API DefluxRuntimeResult deflux_runtime_get_status(DefluxRuntimeStatus* out_status);
DEFLUX_RUNTIME_API const char* deflux_runtime_result_string(DefluxRuntimeResult result);
DEFLUX_RUNTIME_API const char* deflux_runtime_state_string(DefluxRuntimeHostState state);

#ifdef __cplusplus
}
#endif
