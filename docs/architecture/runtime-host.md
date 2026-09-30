# Runtime Host and bootstrap ABI (M1 Task #5)

The legacy DeFluxCore client referenced by historical research notes is intentionally excluded from this archival snapshot.

This document describes the **in-process Runtime Host** and its bootstrap contract. It does not describe a GTA loader. Task #5 does not load DeFlux into GTA.

## 1. Runtime Host responsibility

`runtime/` (`DeFlux_Runtime.dll`) is the DeFlux component that would exist **after** a future, reviewed bootstrap loaded it into a **supported** game process.

It owns DeFlux in-process lifecycle: explicit initialize, status, and shutdown. It does not own:

- launcher / Steam discovery
- save or Story Mode manipulation
- injection / proxy / ASI install
- engine, world, or local-player readiness
- multiplayer session admission

Successful ABI initialization in the standalone harness is **not** approval to operate on the current GTA build.

## 2. Bootstrap vs Runtime

```text
DeFlux Launcher / Bootstrap
        |
        | future reviewed loading mechanism (not in Task #5)
        v
In-Process Runtime Host   (this component)
        |
        v
Game Adapter Runtime      (not implemented in-process in Task #5)
        |
        v
future engine/world/entity integration
```

The **bootstrap** is responsible only for loading/starting the runtime.

The **runtime** owns DeFlux behavior after that start.

The runtime does not know or care whether a future host used a launcher, proxy, or another reviewed mechanism. Task #5 implements only the runtime side and the C ABI a bootstrap would call.

## 3. Runtime lifecycle

Independent of OS process observation (`ProcessLifecycle`) and of multiplayer `SessionState`.

| State | Meaning |
| --- | --- |
| `UNINITIALIZED` | DLL may be loaded; `deflux_runtime_initialize` has not succeeded |
| `INITIALIZING` | Transient under the host lock during initialize |
| `INITIALIZED` | Initialize succeeded; shutdown is allowed |
| `SHUTTING_DOWN` | Transient under the host lock during shutdown |
| `STOPPED` | Shutdown succeeded after initialize; initialize may be requested again |
| `FAILED` | Reserved for initialize work that starts and then fails; M1 has no such work yet |

Duplicate initialize while active (`INITIALIZED` / `INITIALIZING` / `SHUTTING_DOWN`) returns `INVALID_STATE`.

Shutdown is allowed only from `INITIALIZED`. Shutdown before initialize, or a second shutdown, returns `INVALID_STATE`.

## 4. Bootstrap ABI versioning

`DEFLUX_RUNTIME_ABI_VERSION` is `1`.

It is **not** any of:

- DeFlux product SemVer (`kApplication`)
- multiplayer protocol SemVer (`kProtocol`)
- Game Adapter API SemVer (`kGameAdapterApi`)
- GTA executable SHA-256 / file version

Those versions change for different reasons (product release, wire protocol, adapter contract, game patch). The bootstrap ABI changes when exported functions or packed structs are incompatible. `foundation` exposes the same integer as `deflux::version::kRuntimeAbi` so out-of-process tools can print it without loading the DLL.

## 5. ABI ownership and lifetime

Header: `runtime/include/deflux/runtime/abi.h`.

C-compatible, `__cdecl`, packed structs (`#pragma pack(1)`):

| Struct | Packed size |
| --- | --- |
| `DefluxRuntimeInitContext` | 12 |
| `DefluxRuntimeAbiInfo` | 16 |
| `DefluxRuntimeStatus` | 272 |

Rules:

- No C++ STL types, exceptions, or Windows `HANDLE` values across the boundary.
- Caller owns `InitContext`, `AbiInfo`, and `Status` (including the 256-byte diagnostic array).
- `deflux_runtime_result_string` / `deflux_runtime_state_string` return process-lifetime static text, never a pointer into a temporary.
- `struct_size` must equal `sizeof` the struct for that ABI version. Wrong size is `INVALID_ARGUMENT`.
- `InitContext` currently carries ABI version and flags (`flags` must be `0` in M1). No GTA addresses, UE4 pointers, offsets, save/story data, credentials, or process handles.

## 6. Why DllMain remains minimal

`DllMain` only calls `DisableThreadLibraryCalls` on `DLL_PROCESS_ATTACH` and returns `TRUE`.

It must not perform game/Steam discovery, networking, hooks, memory access, thread-heavy work, resource loading, or server connection. The loader lock makes that unsafe. Runtime start is `deflux_runtime_initialize` only.

## 7. Why Task #5 does not load into GTA

The current fingerprint remains `DETECTED_BUT_UNSUPPORTED`. There is no reviewed in-process attach, and this task must not invent one.

`LoadLibraryW` is used only by `DeFlux_Runtime_Harness.exe` to load `DeFlux_Runtime.dll` **into the harness process**. That is ordinary same-process DLL loading, not injection.

## 8. Three independent machines

| Machine | Owner | Example states |
| --- | --- | --- |
| OS process lifecycle | Foundation + GTA adapter `observe()` | `NOT_RUNNING`, `PROCESS_DETECTED`, `PROCESS_EXITED` |
| Runtime host lifecycle | `DeFlux_Runtime` | `UNINITIALIZED` … `STOPPED` / `FAILED` |
| Multiplayer session | Foundation `SessionState` | `DISCONNECTED` … `PLAYING` |

Process presence is not runtime initialization. Runtime initialization is not a multiplayer session.

## 9. Current GTA build remains DETECTED_BUT_UNSUPPORTED

Task #5 does not register SHA-256 `63e633bb51c35fe35ca98f503b62e7ddb8403abe4c9d501cfa9162892453846f`. The Test Client still reports `DETECTED_BUT_UNSUPPORTED` for that vanilla executable.

## 10. Engine / world / player readiness remain future work

`engine_ready`, `world_ready`, and `local_player_ready` stay `NOT_IMPLEMENTED`. The Runtime Host does not call the Game Adapter in Task #5. A future in-process adapter would sit **below** the host; it is not implemented here.

## Dependency direction

```text
legacy client / server / launcher
        (no dependency)

foundation  <----  runtime (DeFlux_Runtime.dll)
foundation  <----  game_adapters/gta_sa_de
foundation  <----  test_client

runtime     <----  tests/runtime harness  (LoadLibrary of the DLL; no .lib link)
```

The runtime depends on M1 `foundation` only (version constants). It does **not** depend on `client/`, `DeFluxCore`, the GTA adapter, or the launcher.

## Result codes

| Code | Name |
| --- | --- |
| 0 | `OK` |
| 1 | `INVALID_ARGUMENT` |
| 2 | `ABI_MISMATCH` |
| 3 | `INVALID_STATE` |
| 4 | `INITIALIZATION_FAILED` (unexpected exception at the C boundary; M1 initialize has no failing work after valid input) |

## Exports

- `deflux_runtime_query_abi`
- `deflux_runtime_initialize`
- `deflux_runtime_shutdown`
- `deflux_runtime_get_status`
- `deflux_runtime_result_string`
- `deflux_runtime_state_string`

## Related

- [Architecture overview](overview.md)
- [Build](../development/build.md)
