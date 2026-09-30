# Task #10B — Current-build static local-ownership discovery

**Status:** Static research only. **Not implemented.** GTA remained closed.
No injection. No live memory read. No commit.

**Checkpoint:** `31ad78e5f86334d424c63695b331dcd6a11287e8`

**Conclusion:** `NO_LIVE_EXPERIMENT_JUSTIFIED`

Task #10A remains `NO_JUSTIFIED_EXPERIMENT`. This task does not strengthen that
result. M07 (`0x050250E8`, `0x05725750`) was not used.

## Build

| Field | Value |
| --- | --- |
| SHA-256 | `63e633bb51c35fe35ca98f503b62e7ddb8403abe4c9d501cfa9162892453846f` |
| Version | 1.0.113.21181 |
| Image base | `0x140000000` |
| SizeOfImage | `0x05CA4400` |
| SHA verified offline | Yes |

Sections read: `.text`, `.rdata`, `.data`, `.pdata`. `_RDATA` was listed and not
used as an ownership source. No RTTI local-player type records
(`.?AVULocalPlayer@@`, `.?AVAPlayerController@@`, `.?AVUGameViewportClient@@`,
`.?AVAPlayerCameraManager@@`) are present.

## Question

What code or data path in this image distinguishes local human ownership from
a generic player, ped, or controller object?

An address is not an answer. A reflection name sitting next to another
reflection name is not an answer.

## Method and bounds

Offline only. Instruction decoding reused the existing unambiguous RIP-relative
`LEA`/`MOV` modrm `0x05` pattern from `local_player_semantic_research.cpp`.
Overlapping matches one byte into a real `LEA` were discarded. Uncertain
decodes were not promoted.

| Bound | Limit | Used |
| --- | --- | --- |
| Semantic anchors retained | 64 | 27 needles; 25 exact C strings plus the embedded `APlayerController` sentence |
| Direct code-reference regions per family | 32 | 3 real regions, all in LOCAL, all already recorded by Task #8E |
| High-interest functions | 24 | 0 functions contained two semantic families |
| Caller/callee functions | 8 | 0. Stage not entered |
| Caller/callee depth | 2 | Not entered |
| Data relationships per selected neighborhood | 16 | Name-pointer neighborhoods, stride observed as `0x30` |
| Recursive pointer walks | none | none |
| Whole-image global ranking | none | none |

This is not a repeat of Task #8E's string-to-nearby-global pass. Stage 5 looked
only at name-pointer neighborhoods after code correlation failed to produce a
multi-family function. No global was chosen as a starting point.

`GUObjectArray`, `FNamePool`, and `GNames` are absent as strings. Object-array
and name-pool evidence did not improve.

## Anchor inventory (exact C strings)

| Family | Present | Absent as exact C string |
| --- | --- | --- |
| INPUT | `PlayerInput`, `InputComponent`, `EnableInput`, `DisableInput`, `InputMode`, `bBlockInput`, `IsLocallyControlled`, `bIsLocalPlayerController`, `Possess`, `UnPossess` (10) | `IgnoreInput`, `InputDevice`, `LocalPlayerInput` |
| VIEWPORT | `GameViewport`, `ViewportClient`, `LocalPlayers` (3) | `UGameViewportClient`. `SplitScreen` exists as a substring only |
| CAMERA | `PlayerCameraManager`, `ViewTarget`, `CameraManager`, `CameraOwner` (4) | `SetViewTarget`, `PlayerCamera` |
| LOCAL PLAYER / CONTROLLER | `LocalPlayer`, `PlayerController`, `AcknowledgedPawn`, `PossessedPawn`, `OwningGameInstance`, `PlayerIndex`, `ControllerId`, `IsLocalPlayerController` (8). `APlayerController` occurs inside a longer sentence at `0x04226158` | `ULocalPlayer`, `GetLocalPlayer`, `GetFirstLocalPlayerController`, `PrimaryPlayer` |

Direct code references, after discarding the one-byte overlap:

| Instruction | String | Function (`.pdata`) | Prior record |
| --- | --- | --- | --- |
| `0x00BE678E` | `APlayerController` | `0x00BE66C0`–`0x00BE6812` | Task #8E |
| `0x03522304` | `PlayerController` | `0x035222F0`–`0x035223CF` | Task #8E |
| `0x03522AA7` | `PlayerController` | `0x03522AA7`–`0x03522AC3` | Task #8E |

No INPUT, VIEWPORT, or CAMERA string has a direct code reference under this
decoder. No function references two families. Caller/callee analysis was not
started.

Name-pointer slots (preferred-base QWORDs in `.rdata` / `.data` aimed at those
C strings) also have **zero** RIP-relative code references.

## Retained routes

| ID | Family | What was observed | Classification |
| --- | --- | --- | --- |
| R-IN-1 | INPUT | `PlayerInput` name slot `0x04A629A0` beside `GetControllerKeyTimeDown` / `WasControllerKeyJustReleased` | **NOT_LOCALITY_EVIDENCE** |
| R-IN-2 | INPUT | `EnableInput` / `DisableInput` name slots near `0x049C4878` | **NOT_LOCALITY_EVIDENCE** |
| R-VP-1 | VIEWPORT | `GameViewport` `0x04A30F90` beside `OwningGameInstance` and `ActiveNetDrivers` | **LOCALITY_CONTRADICTED** |
| R-VP-2 | VIEWPORT | `LocalPlayers` `0x04A431B0` beside `OnlineSession` | **LOCALITY_WEAK** |
| R-CAM-1 | CAMERA | `ViewTarget` `0x04B10508` beside `CameraCachePrivate` / `PendingViewTarget` | **LOCALITY_WEAK** |
| R-CAM-2 | CAMERA | `ViewTarget` `0x04ADFEF0` beside `OpenChannels` / `PktJitter` | **LOCALITY_CONTRADICTED** |
| R-LP-1 | LOCAL | `IsLocallyControlled` beside `IsPlayerControlled` and `OnRep_Controller` | **LOCALITY_CONTRADICTED** |
| R-LP-2 | LOCAL | `PlayerCameraManager` `0x04B0ADA8` beside `AcknowledgedPawn`, `MyHUD`, and a second `PlayerInput` beside `NetConnection` and `LastSpectatorStateSynchTime` | **LOCALITY_CONTRADICTED** |
| R-LP-3 | LOCAL | `IsLocalPlayerController` beside `IsPlayerController`, `OnRep_Pawn`, `ClientSetLocation` | **LOCALITY_WEAK** |
| R-LP-4 | LOCAL | `bIsLocalPlayerController` slot `0x04B0C090`, no neighbor names, no code reference | **LOCALITY_WEAK** |
| R-LP-5 | LOCAL | Task #8E single-anchor functions only | **LOCALITY_WEAK** |

**LOCALITY_STRONG: 0.**
**LOCALITY_PROMISING_UNVERIFIED: 0.**
**LOCALITY_WEAK: 5.**
**LOCALITY_CONTRADICTED: 4.**
**NOT_LOCALITY_EVIDENCE: 2.**
**Retained routes: 11.**

### Why the strongest weak route is still not local

R-LP-3 is the strongest name-level hint because `IsLocalPlayerController`
asserts locality in the identifier. The same name table also contains
`OnRep_Pawn` and `ClientSetLocation`, so the table is a replicated controller
API, not a local-only root. Nothing in `.text` references the name slot. One
identifier is one signal. There is no second signal and no object to observe.

R-LP-4 is the same fact with fewer neighbors.

### Why the controller-shaped table is not local

R-LP-2 shows `PlayerCameraManager` and `AcknowledgedPawn` one stride apart, in
a name list that also contains `MyHUD`, `AddRollInput`, and client RPCs. A
second `PlayerInput` slot sits beside `NetConnection`, `CheatManager`, and
`LastSpectatorStateSynchTime`. That is a generic controller schema that
includes spectator and network state. It does not say which object, if any, is
the local human. It does not depend on GWorld, legacy profiles, or M07, and it
still is not a live candidate.

R-LP-1 places `IsLocallyControlled` on a pawn method list next to
`IsPlayerControlled` and `OnRep_Controller`. The predicate applies to pawns
generally, including pawns that are not the local human. The image also
contains an `AIController` literal, so controller language is not local-only.

R-VP-1 places `GameViewport` next to world/net bookkeeping
(`ActiveNetDrivers`). That is not a viewport-to-local-player chain, and it is
not a new GWorld candidate.

R-CAM-2 places one `ViewTarget` in a network-channel name list. R-CAM-1 places
another in a camera-cache list with no owner link.

No two routes converge on one ownership relationship.

## False positives checked

| Case | Result |
| --- | --- |
| NPC / generic pawn | `IsLocallyControlled` shares a table with generic pawn methods |
| AI controller | `AIController` literal exists; not a local root |
| Network / remote controller | `OnRep_*`, `Client*`, `NetConnection`, `OpenChannels` share the interesting tables |
| Spectator | `LastSpectatorStateSynchTime` shares the controller input neighborhood |
| Frontend / window | one `LocalPlayer` slot sits beside window-focus flags; no pawn link |
| Split screen | `SplitScreen` substring exists; no ownership chain recovered |
| Class schema vs instance | every multi-name hit is a name table, not an instance |
| Generic input device | R-IN-1 is key/touch/controller-key API, not a character |

## Combinations requested

| Combination | Result |
| --- | --- |
| INPUT + CONTROLLER | Same controller name table (R-LP-2), including net and spectator names. Not uniquely local |
| VIEWPORT + LOCALPLAYER | No code function. `GameViewport` meets `OwningGameInstance` in a world/net name table |
| VIEWPORT + CONTROLLER | Not found |
| CAMERA + PLAYERCONTROLLER | `PlayerCameraManager` beside `AcknowledgedPawn` in R-LP-2. Schema only |
| CAMERA + VIEWTARGET + CONTROLLER | Not in one neighborhood |
| LOCALPLAYER + PLAYER INDEX | Not in one neighborhood. `ControllerId` is beside `bIsServer`, not `LocalPlayers` |
| LOCALPLAYER + VIEWPORT | Not established. `LocalPlayers` is beside `OnlineSession` |
| LOCALPLAYER + INPUT | Not established as an ownership chain |

## Live gate

`NO_LIVE_EXPERIMENT_JUSTIFIED`

Missing evidence: a current-build **code** path that consumes a uniquely local
owner (input bound to one local controller, a viewport client bound to one
local player, or a predicate actually used to select that controller), plus a
second independent support, plus a passive observation that can falsify it.
Reflection name adjacency does not provide that path. No engine call is
proposed. No address is promoted. No address was read from a live process.

A live experiment is not justified.

## Static-research boundary

`IsLocalPlayerController`, `bIsLocalPlayerController`, `LocalPlayers`,
`PlayerInput`, and `PlayerCameraManager` are semantic names only. They do not
establish object identity, local ownership, field layout, a live address, a
player instance, or readiness. Name adjacency is not runtime evidence.

R-LP-3 stays **LOCALITY_WEAK**. Its evidence is the `IsLocalPlayerController`
identifier in a method-name table. There is no code reference, no instance, no
uniquely local owner, no ownership chain, no independent convergence, and no
passive live observation. R-LP-4 stays **LOCALITY_WEAK** for the same reason.

## Closed strategy

Repeating string → xref → nearby global → candidate is not justified without
materially new evidence. Task #7 GWorld discovery, Task #8 legacy chains, M07,
coordinate scanning, and arbitrary pointer chains stay closed.

## Next direction (not started)

**Task #10C — Current-Build Runtime Metadata Discovery Design** is a name only.
It requires separate architect authorization. It is not an approved live
experiment. It is not started here.

## Trust boundary

| Boundary | Result |
| --- | --- |
| Task #10A | **`NO_JUSTIFIED_EXPERIMENT`** |
| Task #10B | **`NO_LIVE_EXPERIMENT_JUSTIFIED`** |
| Local-player identity | **NOT ESTABLISHED** |
| Local-player transform | **NOT ESTABLISHED** |
| Uniquely-local root | **NONE** |
| GWorld | **NO QUALIFIED CANDIDATE** |
| GUObjectArray-like evidence | **ABSENT** |
| FName / name-pool evidence | **ABSENT** |
| UObject / class metadata | **NAME TABLES ONLY. NO INSTANCE IDENTITY** |
| Production registry | **EMPTY** |
| Compatibility | **DETECTED_BUT_UNSUPPORTED** |
| Readiness | **NOT_IMPLEMENTED** |

Every RVA above is **STATIC_RESEARCH_ONLY**.

## Related documents

- [Runtime metadata discovery design](runtime-metadata-discovery-design.md) — Task #10C: no metadata experiment
- [Local-player identity experiment design](local-player-identity-experiment-design.md) — Task #10A, unchanged
- [Local-player semantic research](local-player-semantic-research.md) — Task #8E
