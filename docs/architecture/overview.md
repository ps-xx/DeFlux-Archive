# Architecture overview

> DISCONTINUED. Maintenance: none. Playable multiplayer: no. Supported builds: none. Compatibility: `DETECTED_BUT_UNSUPPORTED`. Readiness: `NOT_IMPLEMENTED`. GWorld: no qualified candidate. Local-player identity and transform: not established.

This document is the historical **target** architecture. The code in this snapshot is unfinished research. It is not a finished multiplayer platform.

The legacy DeFluxCore client referenced by historical research notes is intentionally excluded from this archival snapshot.

## Current status

The repository is an archival snapshot of discontinued research. It is not an architecture transition still in progress.

### LEGACY PROTOTYPE

The existing trees in the private archive were `client`, `server`, `launcher`, `shared`, and `vendor`. This snapshot keeps server, launcher, and shared sources. It does not include `client/` or `vendor/`.

An earlier legacy client, intentionally excluded from this snapshot, attempted an injected DLL, a UE4 pointer chain, position writes, and a Grove Street teleport. Those attempts were not accepted as proven local-player identity or transform. See [NEGATIVE_RESULTS.md](../NEGATIVE_RESULTS.md).

### NEW M1 FOUNDATION

A small, GTA-independent foundation now exists beside the prototype:

- `foundation/` — version model, session-state model, process lifecycle model, Game Adapter contract, runtime ABI version constant
- `game_adapters/gta_sa_de/` — GTA SA DE adapter. Early stages are passive detection, SHA-256 fingerprinting, and an empty supported-build registry. Later research added process-memory reads, signature scans, and a research DLL loader (`WriteProcessMemory` of a path plus `CreateRemoteThread`). Readiness stays `NOT_IMPLEMENTED`. No build was registered as supported.
- `runtime/` — in-process Runtime Host DLL and versioned C bootstrap ABI. The M1 harness loads it into the harness process. A later research loader can load a DLL into another process; that is not production support.
- `test_client/` — headless DeFlux Test Client (prints versions and passive detection; no sockets, no GTA attach, no runtime DLL load into GTA)
- `tests/foundation/` — self-contained foundation and detection tests (do not require GTA to be running)
- `tests/runtime/` — ABI harness that `LoadLibrary`s the runtime DLL into **its own process**

Finding a process is not engine, world, or player readiness. Offline tests of the Runtime Host do not prove live GTA integration. There is still no resource system, scripting host, server browser, or proven multiplayer session.

Do not treat features in the prototype as shipped platform capabilities.

## Long-term logical components

```text
                    +----------------+
                    |    Launcher    |
                    +--------+-------+
                             |
                             v
                    +----------------+
                    |     Client     |
                    +--------+-------+
                             |
                             v
                    +----------------+
                    |  Game Adapter  |  <-- GTA SA DE implementation lives here
                    +--------+-------+
                             |
                             v
                      GTA SA DE process

  +----------------+     shared protocol / types     +------------------+
  | Dedicated      | <-----------------------------> | Client           |
  | Server         |                                 | Test Client      |
  +----------------+                                 +------------------+
          ^
          |
     +----+----+
     | Shared  |
     +---------+
```

| Component | Role |
| --- | --- |
| **Launcher** | Starts the game and DeFlux session without becoming the multiplayer simulation. Owns process launch, deploy of loaders, and user-facing start flow. |
| **Client** | Multiplayer client: session lifecycle, networking, prediction/interpolation policy, local UI that is not the game engine. Must not own GTA memory addresses. |
| **Game Adapter** | Isolates *Grand Theft Auto: San Andreas – The Definitive Edition* details. Passive detection and fingerprinting came first. Later research code can read process memory and load a research DLL. It does not establish a supported build, GWorld, or a local-player transform. |
| **Dedicated Server** | Authoritative simulation and session admission. Does not embed the game renderer. |
| **Shared** | Protocol constants, packet schemas, and types used by more than one component. Game-specific offsets do not belong here. |
| **Test Client** | Headless or minimal peer for protocol and session tests, independent of GTA. M1 provides a console host that prints versions/state and instantiates the adapter skeleton. |

### Isolation rule

Game-specific GTA SA DE implementation details must eventually sit behind the Game Adapter.

The multiplayer core (Client, Dedicated Server, Shared, Test Client) must **not** directly depend on:

- GTA memory addresses
- UE4 pointer chains
- CJ-specific assumptions
- story mission logic
- game-specific hooks

Those concerns lived in the omitted legacy client. They are not the foundation of this snapshot. The legacy DeFluxCore client referenced by historical research notes is intentionally excluded from this archival snapshot.

## Mapping onto this snapshot

The folders below are what this snapshot contains. They are **not** a 1:1 implementation of the long-term table above.

### What this snapshot contains

| Folder | What it is in this snapshot |
| --- | --- |
| `launcher/` | Native launcher, optional Node NUI host, `version.dll` proxy. The clean-save file is not included. |
| `server/` | UDP server prototype and in-memory player registry. Not a proven game server. |
| `shared/` | Protocol and player-state types. |
| `foundation/` | Version, session state, process lifecycle, `IGameAdapter`. No GTA dependency. |
| `game_adapters/gta_sa_de/` | Detection, fingerprint, qualification reads, research scans, and a research DLL loader. Supported-build table is empty. |
| `test_client/` | Headless console test client. |
| `tests/foundation/` | Offline tests. They do not launch GTA. |
| `runtime/` | In-process runtime host and C ABI. |
| `tests/runtime/` | Same-process ABI harness. |
| `research_launcher/` | Thin console host for the adapter research entry point. |
| `tests/research_loader_fixture/` | Offline loader fixture. |

`client/` and `vendor/minhook/` are not in this snapshot. The legacy DeFluxCore client referenced by historical research notes is intentionally excluded from this archival snapshot. See [NEGATIVE_RESULTS.md](../NEGATIVE_RESULTS.md).

Solution: `DeFlux.sln`. Projects: `DeFlux_Server`, `DeFlux_Launcher`, `version_proxy`, and the M1 folder `DeFlux_Foundation`, `DeFlux_GameAdapter_GtaSaDe`, `DeFlux_TestClient`, `DeFlux_Foundation_Tests`, `DeFlux_Runtime`, `DeFlux_Runtime_Harness`. `build.cmd Debug M1` also builds `research_launcher` and `tests/research_loader_fixture`.

Dependency direction (M1): `foundation` ← `game_adapters/gta_sa_de`; `foundation` ← `test_client`; `foundation` ← `runtime`. The runtime harness loads `DeFlux_Runtime.dll` at runtime and does not link its import library. Foundation never depends on the GTA adapter, runtime, launcher, or legacy client/server. The runtime does not depend on the GTA adapter or on `client/`.

## Session lifecycle (target)

Network sessions use an explicit state machine. Conceptual states:

1. `DISCONNECTED`
2. `CONNECTING`
3. `HANDSHAKING`
4. `CONNECTED`
5. `WORLD_LOADING`
6. `READY`
7. `SPAWNING`
8. `PLAYING`

Admission is explicit. Unknown or unauthenticated endpoints must not become player sessions by side effect (for example, because a sync or spawn packet arrived first).

## Timing (target)

Three clocks stay independent:

- game frame rate
- client networking rate
- server simulation tick

None of these may be assumed equal to the others. The prototype currently mixes a software ~25 Hz client tick with a 60 Hz server loop and game frames; that coupling is not the target.

## What this architecture must allow later

The component split is there so later milestones can add, without rewriting the core around GTA:

- remote player entities
- vehicles
- generic network entities
- a resource system
- scripting
- a server browser
- headless test clients

Those systems are **not** implemented in M1. The Test Client exists only as a headless foundation host.

## M1 Task #3 — passive GTA SA DE detection

Owned entirely by `game_adapters/gta_sa_de/`. Foundation headers stay free of Windows, Steam, and GTA executable names.

- **Install discovery:** Steam registry (`SteamPath` / `InstallPath`) plus `libraryfolders.vdf` and `appmanifest_1547000.acf`, then `Gameface/Binaries/Win64/SanAndreas.exe` (fallback `Gameface-Win64-Shipping.exe`). No hardcoded user path. The adapter does not depend on the legacy launcher.
- **Process detection:** `CreateToolhelp32Snapshot` + `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` + `QueryFullProcessImageNameW`. No VM read/write, injection, or hooks.
- **Fingerprint:** SHA-256 of the executable file (BCrypt), plus optional PE file/product version and size. The whole install is not hashed.
- **Supported-build registry:** explicit SHA-256 allow-list inside the adapter. Task #3 ships it **empty**. Unknown hashes are `DETECTED_BUT_UNSUPPORTED`. Missing exe is `NOT_FOUND`. Fingerprint/path failures are `ERROR`. Fail closed: never `SUPPORTED` unless registered.
- **Readiness:** `engine_ready` / `world_ready` / `local_player_ready` remain `NOT_IMPLEMENTED`. A running process is not runtime readiness.
- **No modification:** detection does not launch GTA, inject, write ASI/`version.dll`, or touch game/Documents files.

## M1 Task #4 — passive process observation

Separate from multiplayer `SessionState`. Foundation owns a game-agnostic process lifecycle:

`NOT_RUNNING` → `PROCESS_DETECTED` → `PROCESS_EXITED` → `NOT_RUNNING`

`OBSERVATION_ERROR` is distinct from `NOT_RUNNING`. `PROCESS_EXITED` is a one-shot transition, not a sticky state.

- **Identity:** PID + opaque `creation_identity` (Windows process creation FILETIME as uint64) + verified UTF-8 image path. Same PID with a different creation stamp is a new instance.
- **Polling:** `GtaSaDeAdapter::observe()` is an explicit poll. No observer thread. The adapter does not own host lifetime.
- **Path verification:** process image path is normalized (`/` → `\`, `GetFullPathNameW`, `GetLongPathNameW`) and compared case-insensitively to the detected installation executable. Filename-only matches are rejected. If the image cannot be verified, the process is not accepted as GTA.
- **Access:** `CreateToolhelp32Snapshot`, `Process32FirstW`/`Process32NextW`, `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)`, `QueryFullProcessImageNameW`, `GetProcessTimes`. Handles are short-lived and internal.
- **Not implied:** process presence ≠ build compatibility, runtime attachment, engine/world/player readiness, or multiplayer `CONNECTED`.
- **Supported-build policy:** unchanged. Observed vanilla SHA-256 remains unregistered (`DETECTED_BUT_UNSUPPORTED`).
- **No modification:** observation does not launch, terminate, inject, hook, or write GTA/Documents files.

## M1 Task #5 — in-process Runtime Host + bootstrap contract

Owned by `runtime/` with ABI tests in `tests/runtime/`. See [Runtime Host](runtime-host.md).

- **Separation:** bootstrap (future loading mechanism) vs runtime (DeFlux behavior after load). Task #5 implements only the runtime and the C ABI.
- **Lifecycle:** `UNINITIALIZED` → `INITIALIZING` → `INITIALIZED` → `SHUTTING_DOWN` → `STOPPED`, plus `FAILED`. Not `SessionState` and not `ProcessLifecycle`.
- **ABI:** version `1`, packed C structs, explicit result codes. No STL, exceptions, or `HANDLE`s across the boundary. `DllMain` only disables thread notifications.
- **Harness:** `DeFlux_Runtime_Harness` uses `LoadLibraryW` / `GetProcAddress` / `FreeLibrary` in its **own** process. It is not a GTA injector.
- **Compatibility:** current vanilla fingerprint stays `DETECTED_BUT_UNSUPPORTED`. `engine_ready` / `world_ready` / `local_player_ready` stay `NOT_IMPLEMENTED`.
- **No GTA load in Task #5:** that task's runtime host does not itself remote-load, hook, or read game memory. A later research loader in the game adapter does allocate remote memory and start `LoadLibraryW`. See [NEGATIVE_RESULTS.md](NEGATIVE_RESULTS.md). That loader did not change compatibility or readiness.

## M1 Task #6 — read-only runtime qualification

Owned by `game_adapters/gta_sa_de/`. See [Runtime qualification](runtime-qualification.md).

- **External probe:** Test Client / `GtaSaDeAdapter::qualify()`. Does not load `DeFlux_Runtime.dll` into GTA.
- **Access:** `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ)` and `ReadProcessMemory` inside the adapter only. No writes, hooks, or injection.
- **Identity:** requires Task #4 verified process; re-hashes the on-disk executable; aborts if path, instance, or fingerprint changes.
- **Module:** ASLR-safe Toolhelp main-module discovery; PE32+ AMD64 headers validated remotely.
- **Anchor:** one GWorld slot RVA, build-profile only. Conservative pointer/vtable checks. No player/actor walk.
- **Public compatibility:** vanilla SHA remains `DETECTED_BUT_UNSUPPORTED`. `QUALIFIED` is a separate result.
- **Readiness:** `engine_ready` / `world_ready` / `local_player_ready` stay `NOT_IMPLEMENTED`.

## Related documents

- [Architectural principles](principles.md)
- [Runtime Host and bootstrap ABI](runtime-host.md)
- [Runtime qualification](runtime-qualification.md)
- [Build](../development/build.md)
