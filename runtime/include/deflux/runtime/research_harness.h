#pragma once

// M1 research harness exports (DeFlux_Runtime.dll). Not part of the stable generic
// runtime ABI in abi.h. Used by DeFlux_Runtime_Harness for module-boundary proof only.

#include "deflux/runtime/abi.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFLUX_RUNTIME_API int32_t deflux_runtime_research_substate(void);
DEFLUX_RUNTIME_API uintptr_t deflux_runtime_research_controller_identity(void);
DEFLUX_RUNTIME_API DefluxRuntimeResult deflux_runtime_research_harness_async_shutdown_test(void);
DEFLUX_RUNTIME_API DefluxRuntimeResult deflux_runtime_research_harness_async_failure_shutdown_test(
    void);
DEFLUX_RUNTIME_API int32_t deflux_runtime_research_bootstrap_complete(void);
DEFLUX_RUNTIME_API DefluxRuntimeResult deflux_runtime_research_harness_auto_stop_stress_test(
    void);
DEFLUX_RUNTIME_API DefluxRuntimeResult deflux_runtime_research_harness_async_shutdown_stress_test(
    void);
DEFLUX_RUNTIME_API DefluxRuntimeResult deflux_runtime_research_harness_async_failure_stress_test(
    void);
DEFLUX_RUNTIME_API DefluxRuntimeResult deflux_runtime_research_harness_lifecycle_breadcrumb_smoke(
    void);

#ifdef __cplusplus
}
#endif
