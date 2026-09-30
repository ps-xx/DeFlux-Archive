# Runtime qualification (M1 Task #6)

The legacy DeFluxCore client referenced by historical research notes is intentionally excluded from this archival snapshot.

External compatibility probe of a **verified** GTA SA DE process. Task #6 qualification reads memory and does not write it. It does not, by itself, load `DeFlux_Runtime.dll` into GTA. A separate later research loader does. Neither path marks the build supported.

## Purpose

Answer, for one poll:

1. Is this the exact fingerprint the adapter has a qualification profile for?
2. Can the main module be identified under ASLR?
3. Are PE headers readable and structurally valid?
4. Can one GWorld slot pointer be read and conservatively validated?
5. Did every required check pass?

A pass is `QUALIFIED` (`RUNTIME_QUALIFICATION_PASSED` for these checks only). It is **not** public `SUPPORTED` and **not** engine/world/player readiness.

## Fingerprint support vs runtime qualification

| Concept | Owner | Task #6 status for the vanilla SHA-256 |
| --- | --- | --- |
| Executable fingerprint compatibility | `supported_builds` allow-list | `DETECTED_BUT_UNSUPPORTED` |
| Process observation | Task #4 identity | unchanged |
| Runtime qualification | internal build profile + read-only probe | may be `QUALIFIED` without changing the allow-list |
| Runtime host lifecycle | `DeFlux_Runtime` ABI | unused; DLL is not loaded into GTA |
| Engine / world / local-player readiness | `IGameAdapter` | still `NOT_IMPLEMENTED` |
| Multiplayer session | `SessionState` | unused |

`DETECTED_BUT_UNSUPPORTED` may coexist with `QUALIFIED`.

## External read-only probe model

The Test Client (or any out-of-process host) calls `GtaSaDeAdapter::qualify()`.

Before any live probe, the adapter scans the verified executable directory for legacy contamination (`version.dll`, `DeFluxCore.asi`, etc.). If found, status is `ENVIRONMENT_CONTAMINATED`: no `OpenProcess`, no memory reads. See [Vanilla validation](../development/vanilla-validation.md).

When the environment is clean, the adapter opens the Task #4 process with `PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ`, reads, then closes the handle. No handle is cached across process-instance changes.

## Process identity requirement

Qualification starts from `ProcessObservation` in `PROCESS_DETECTED` with a valid PID + `creation_identity` + verified image path.

Filename-only `SanAndreas.exe` matches are rejected (Task #4). Before and after memory reads, the adapter re-queries identity. Path, fingerprint, or instance changes abort (`BUILD_MISMATCH` / `PROCESS_CHANGED`).

## ASLR-safe module discovery

`CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32)` + `Module32FirstW` / `Module32NextW`. The main module is the entry whose path matches the verified executable. Base and size come from the snapshot. There is no hardcoded module base.

## Selected first runtime anchor

**GWorld slot** (a pointer-sized global that should hold `UWorld*`).

Location: internal profile RVA `0x05391B70` for SHA-256 `63e633bb51c35fe35ca98f503b62e7ddb8403abe4c9d501cfa9162892453846f` only. Trust level: **`LEGACY_CANDIDATE_UNVERIFIED`** (`kCandidateRvas[0]` from legacy `client/src/game/ue4_memory.cpp`). Task #6 live validation was **`INVALID_FOR_VANILLA_VALIDATION`** because legacy DeFluxCore was active; do not treat this RVA as a trusted current-build locator. The second legacy candidate `0x0FE1D1D8` is rejected: it cannot fit this image.

## Exact validation guarantees

A `QUALIFIED` result means all of:

- verified process still matches Task #4 identity
- on-disk SHA-256, size, and file version match the qualification profile
- main module path/base/size are plausible
- PE is PE32+ AMD64, `SizeOfImage` matches the live module size
- slot RVA is inside the image and in a non-executable section
- slot read is complete
- pointer is 8-byte aligned, canonical user-space, **not** inside the main module (heap object)
- first 8 bytes at that object (vtable) are a complete read, canonical, aligned, and **inside** the main module

It does **not** prove the object is `UWorld`, that a world is playable, or that a local player exists. Null GWorld (typical at some menus) is `ANCHOR_NOT_FOUND`.

## Limitations

- Single-shot; no wait/poll for world load.
- No pattern scan, no pawn/player/vehicle walk, no coordinates.
- No GTA memory writes, hooks, or injection.
- Unknown fingerprints have no profile (`PROFILE_MISSING`).
- Public supported-build registry stays empty.

## Why readiness remains NOT_IMPLEMENTED

A validated pointer slot is not a readiness semantic. `engine_ready` / `world_ready` / `local_player_ready` stay `NOT_IMPLEMENTED` until a later reviewed task defines those states.

## Reverse-engineering note

Inspected: `client/src/game/ue4_memory.cpp`, `local_player.cpp`, `sa_interface_boot.cpp`, `mp_world.cpp`, `hook_engine.cpp`.

| Concept | Class | Disposition |
| --- | --- | --- |
| GWorld as the first world-side anchor | PROVEN CONCEPT | Migrated as the sole Task #6 anchor |
| ASLR module base/size | PROVEN CONCEPT | Toolhelp, not `GetModuleHandle` (that is in-process only) |
| PE DOS/NT sanity | PROVEN CONCEPT | Migrated, bounds-checked, remote via `ReadProcessMemory` |
| Canonical user-space pointer filter (`>= 0x10000`, `< 0x00007FFFFFFFFFFF`) | PROVEN CONCEPT | Migrated |
| Complete reads; no fabricated fallback | PROVEN CONCEPT | Migrated |
| Build-specific GWorld *slot RVA* `0x05391B70` for this SHA | LEGACY_CANDIDATE / UNVERIFIED | Profile-only; not public `SUPPORTED`; Task #6 live pass invalidated |
| `.text` pattern scan for `mov rax,[rip+disp]; test rax,rax` | LEGACY HEURISTIC | **Rejected** — too many false positives without pawn proof |
| `GWorld` / `PersistentLevel` string scans | LEGACY HEURISTIC | **Rejected** |
| Offset-profile pawn/camera chain and position checks | LEGACY HEURISTIC | **Rejected** — Task #6 must not walk to the player |
| Absolute runtime addresses | REJECTED | Never used |
| `0x0FE1D1D8` GWorld candidate | REJECTED | RVA larger than this image |
| `VirtualProtect` / position writes / teleports | REJECTED | Writes |
| MinHook / trampolines / menu hooks / story skip | REJECTED | Gameplay and injection |

## Related

- [Architecture overview](overview.md)
- [Runtime Host](runtime-host.md)
