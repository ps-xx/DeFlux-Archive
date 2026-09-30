# Task #10A — Local-player identity experiment design

**Status:** Audit and design only. **Not implemented.** GTA remained closed. No injection.

**Checkpoint:** `718b8dcb03995d298dfab3e795bb3a84b1373fbc` (Task #9B closed)

**Conclusion:** `NO_JUSTIFIED_EXPERIMENT`

Task #9B validated a bounded in-process **PASSIVE_READ** host. That is access
infrastructure. It does not identify the local player. This audit found no
repository-supported root that answers “why is this specifically the local
human-controlled player?” without reopening closed Task #7 / #8 paths or
guessing a new chain.

No experiment ID is authorized. No local-player candidate is promoted.

| Boundary | Result |
| --- | --- |
| Local-player identity | **NOT ESTABLISHED** |
| Local-player transform | **NOT ESTABLISHED** |
| GWorld | **NO QUALIFIED CANDIDATE** |
| Production supported-build registry | **EMPTY** |
| Compatibility | **DETECTED_BUT_UNSUPPORTED** |
| Readiness | **NOT_IMPLEMENTED** |
| Task #9B activation pipeline | **VALIDATED** |
| Task #10A | **`NO_JUSTIFIED_EXPERIMENT`** |

---

## 1. Closed paths (unchanged)

| Path | Status |
| --- | --- |
| `0x05391B70` | `LEGACY_CANDIDATE_UNVERIFIED`. Non-null in MENU/LOADING. NULL in observed gameplay. Not GWorld-qualified. |
| `0x05764130` | Stable live global. Structurally observed. Identity unproven. Not GWorld-qualified. |
| `0x0FE1D1D8` | Outside the current image. Invalid for this build. |
| Legacy local-player profile chains | `LEGACY_CHAIN_INSUFFICIENT`. Closed (Task #8D). |
| Offline UE string/xref correlation | `NO JUSTIFIED LIVE LOCAL-PLAYER CANDIDATE` (Task #8E). |
| `gGameState` | Confirmed in-process only as MENU → byte 7. Not world, engine, or local-player readiness. |

---

## 2. Access versus identity

The validated in-process host proves **access capability**. It does not prove
**local-player identity**.

A readable pointer is not identity. A structurally plausible object is not
identity. A player-like class is not necessarily local. A controller/pawn
relationship is not necessarily locally owned. A module-resident first qword,
a vtable-shaped value, or a player-like string is access or shape evidence only.

Identity requires a root that is uniquely the local human session (viewport,
local input, or an equivalent ownership record) plus a bounded relationship to
one controlled object, observed across explicit operator phases, and rejected
against NPCs, frontend objects, class defaults, and stale objects.

In-process execution changes **how** a byte can be read. It does not create
that root. The only in-process signal validated live is the `gGameState` byte.

---

## 3. Mechanism classification

Counts below are identity-relevant mechanisms present in M1 or legacy code, or
recorded as research results. Search tokens with no located hit are listed as
absent, not as mechanisms.

| ID | Source | Current / legacy | Classification | Proves | Does not prove | GWorld | Hardcoded offsets | Write | Hook | Engine call | Passive in-process |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| M01 | `ggame_state` / in-process sampler | Current | **EVIDENCE_WEAK** | One derived byte. MENU correlated as 7. | Local player, readiness, post-frontend phase | No | Derivation from `menu_mainpresents`, not a player offset | No | No | No | Yes (already done) |
| M02 | `LocalPlayer` strings (11), 0 code xrefs | Current offline | **EVIDENCE_WEAK** | The literal exists in `.rdata` | Any object, owner, or chain | No | String RVAs only | No | No | No | N/A (static) |
| M03 | `AcknowledgedPawn` string, 0 xrefs | Current offline | **EVIDENCE_WEAK** | Literal presence | Pawn identity or ownership | No | No | No | No | No | N/A |
| M04 | `OwningGameInstance` string, 0 xrefs | Current offline | **EVIDENCE_WEAK** | Literal presence | Game-instance identity | No | No | No | No | No | N/A |
| M05 | `APlayerController` xref → fn `0x00BE66C0` | Current offline | **EVIDENCE_WEAK** | One function mentions one string | Controller object or local ownership | No | Function RVA | No | No | No | Not without a new hypothesis |
| M06 | `PlayerController` xrefs → `0x035222F0` / `0x0497FD20` | Current offline | **EVIDENCE_WEAK** | A different literal is referenced | Link to `APlayerController` or a live object | No | RVAs | No | No | No | No |
| M07 | `.data` reads `0x050250E8`, `0x05725750` from M05 | Current offline | **EVIDENCE_PROMISING_UNVERIFIED** | Two globals are read beside one class string | That either global is the local player | No | Yes, if probed | No | No | No | Possible, but not justified (Task #8E) |
| M08 | Global `0x05087380` from M06 | Current offline | **EVIDENCE_WEAK** | One read site | Identity | No | Yes, if probed | No | No | No | Not justified |
| M09 | `0x05764130` structural prefix | Current | **EVIDENCE_WEAK** | Stable slot; module-resident first qword in one probe | GWorld or local player | Treated as opaque, not qualified | Yes | No | No | No | Already probed externally |
| M10 | `0x05391B70` | Current + legacy | **EVIDENCE_INVALID** | Historical non-null/NULL phase split | GWorld or local player | Yes, legacy assumption | Yes | No | No | No | Closed |
| M11 | `0x0FE1D1D8` | Legacy | **EVIDENCE_INVALID** | Nothing on this image | — | Yes | Yes | No | No | No | No |
| M12 | Legacy `kProfiles` chain | Legacy | **EVIDENCE_INVALID** | Live diagnostic `LOCAL_PLAYERS_BAD` | Local player | Yes | Yes | No | No | No | Closed |
| M13 | `scan_gworld_candidates` | Legacy | **EVIDENCE_INVALID** | Pattern hits exist | Identity | Yes | Heuristic | No | No | No | Forbidden |
| M14 | Secondary GWorld string scan | Legacy | **EVIDENCE_INVALID** | Strings exist | A world root | Yes | Heuristic | No | No | No | Forbidden |
| M15 | PersistentLevel actor scan | Legacy | **EVIDENCE_INVALID** | A level array can be walked if a root exists | The first actor is the local player | Yes | Yes | No | No | No | Closed |
| M16 | `discover_offsets` | Legacy | **EVIDENCE_INVALID** | Brute force can return a plausible chain | Identity | Yes | Search | No | No | No | Forbidden |
| M17 | Grove Street fallback coordinates | Legacy | **EVIDENCE_INVALID** | A constant can be printed | Position or identity | No | Hardcoded coords | No | No | No | Forbidden |
| M18 | `set_position_address` | Legacy | **EVIDENCE_INVALID** | Dead API | A live address | No | Caller-supplied | No | No | No | No |
| M19 | Present hook stub | Legacy | **EVIDENCE_INVALID** | Unused | Frame or player | No | No | No | Intended | No | No |
| M20 | `UnrealWindow` title/class | Legacy | **EVIDENCE_WEAK** | A window can match | Gameplay or local player | No | Title string | No | No | No | Not identity |
| M21 | Gameplay gate `Sleep` after byte 9 | Legacy | **EVIDENCE_WEAK** | A timer was used | That a controller exists | No | Timing | No | No | No | Forbidden as proof |
| M22 | `try_write_position` | Legacy | **MODIFICATION_ONLY** | A write primitive existed | — | Yes | Yes | Yes | No | No | No |
| M23 | Menu inline hook / `StartNewGame` | Legacy | **MODIFICATION_ONLY** | Frontend can be patched | Identity | No | Yes | Yes | Yes | Yes | No |
| M24 | Frontend poll calling natives | Legacy | **MODIFICATION_ONLY** | Legacy called game code | Identity | No | gGameState | No | Indirect | Yes | No |
| M25 | `SpScriptGuard` | Legacy | **MODIFICATION_ONLY** | Script bytes can be changed | Identity | No | Script symbols | Yes | No | No | No |
| M26 | `MpWorld` INI writes | Legacy | **MODIFICATION_ONLY** | Documents files can be edited | Identity | No | INI keys | Filesystem | No | No | No |

**EVIDENCE_STRONG (local-player identity): 0.**

**EVIDENCE_PROMISING_UNVERIFIED: 1** (M07). Task #8E already forbids promoting
M07 to a live read: one class string, two uncorroborated globals, no
multi-anchor function.

**EVIDENCE_WEAK: 10** (M01–M06, M08, M09, M20, M21).

**EVIDENCE_INVALID: 10** (M10–M19).

**MODIFICATION_ONLY: 5** (M22–M26).

**Total identity-relevant mechanisms: 26.**

### Absent (searched, not found as current-build evidence)

- `GUObjectArray` / object-array layout: search token only in
  `gworld_identity_analysis.cpp`. No located array, count, or chunk table.
- `FName` / name pool: no located pool.
- `UObject` header layout on this build: hypothesis text only.
- `UGameViewportClient`, `PlayerCameraManager`, view target: documented missing
  in `runtime-readiness-research.md`.
- RTTI local-player type records: not established.
- Input path that names the local controller: not established.
- Bounded class-name resolution: not feasible from current evidence. Stock UE4
  layouts and foreign-game offsets are not imported.

---

## 4. Local-ownership routes

| Route | Root uniquely local? | Evidenced relationships | Hypotheses | Depends on GWorld | Depends on legacy profiles | Passive? |
| --- | --- | --- | --- | --- | --- | --- |
| Viewport → local player → controller → pawn | Would be, if a viewport root existed | None | Entire chain | Unknown | No | Not available |
| Input → controller → pawn | Would be, if an input owner existed | None | Entire chain | Unknown | No | Not available |
| GWorld → game instance → local players → controller → pawn | No. World root is not local ownership, and no qualified GWorld exists | Legacy profiles failed live | Closed | Yes | Yes | Closed |
| Single-anchor globals M07 | No. A data read beside one string is not local ownership | Read sites only | That the global is a controller | No | No | Technically readable; **not justified** |
| `gGameState` byte | No. Frontend signal only | MENU = 7 in-process | None for identity | No | No | Already observed |

Falsifier for any future route: the candidate is non-null in MENU while no
human pawn exists, is shared with an NPC, disappears without player change, or
matches a class-default / frontend object.

---

## 5. Why no experiment is proposed

A justified first experiment needs one evidenced, uniquely local root and a
bounded passive observation that can be falsified. This repository does not
have that root.

Designing a live read of M07, a new GWorld guess, an object-array walk, a
coordinate scan, or an engine call would repeat forbidden shortcuts. An engine
or game call is **not** authorized. Passive observation is not “insufficient
because we need a call”; it is insufficient because there is no identity
hypothesis to observe.

**Experiment ID:** none.

**Maximum conclusion:** `NO_JUSTIFIED_EXPERIMENT`.

Not `LOCAL_PLAYER_FOUND`. Not `LOCAL_PLAYER_READY`. Not `WORLD_READY`.

---

## 6. Future phase correlation (design only, not implemented)

Any later live experiment must record operator phase marks **during** the run,
in the research evidence stream, not reconstructed afterward.

Smallest design:

- The research launcher accepts an explicit mark command while a session is
  already running (for example a second local intent record or a phase byte in
  the existing status mapping).
- Allowed labels: `MENU`, `LOADING`, `GAMEPLAY_STANDING`, `GAMEPLAY_MOVING`,
  `PAUSE_MENU`.
- Each mark appends `PHASE_MARK label time_ms` to the same evidence log and is
  flushed immediately.
- Marks are operator observations. They are not readiness.

This exists so a future session cannot end with “timestamps were not recorded.”
It is not implemented here, and it does not by itself justify a local-player
experiment.

---

## 7. What would reopen a design

New **independent** evidence, reviewed before any live read, such as:

- a static derivation, on this exact SHA, from a uniquely local root
  (viewport, input, or equivalent) to one object, with at least two
  corroborating anchors; or
- a located, build-specific object/name metadata structure whose layout is
  derived from this image, not from another Unreal title.

Until that evidence exists, Task #10 does not proceed to implementation or
injection.

---

## 8. Next direction

**Task #10B — Current-Build Static Local-Ownership Discovery** was named here
and later recorded in
[local-ownership-static-discovery.md](local-ownership-static-discovery.md).
That record does not change this conclusion. Task #10A remains
`NO_JUSTIFIED_EXPERIMENT`.

That search stayed on this binary and on input, viewport, camera, and local
controller semantics. It did not start from GWorld or from M07
(`0x050250E8`, `0x05725750`). It did not retry legacy profiles, scan
coordinates, assume a stock UE4 layout, or authorize a live experiment.

M07 stays **EVIDENCE_PROMISING_UNVERIFIED**. It is not a justified live
candidate. It is not read, promoted, or probed here. Task #8E's restriction
remains in force.

---

## Related documents

- [Runtime metadata discovery design](runtime-metadata-discovery-design.md) — Task #10C: no metadata experiment
- [Local-ownership static discovery](local-ownership-static-discovery.md) — Task #10B: no live experiment
- [Local-player semantic research](local-player-semantic-research.md) — Task #8E negative result
- [GWorld discovery research](gworld-discovery-research.md) — Task #7 negative result
- [In-process research design](in-process-research-design.md) — Task #9B pipeline close
- [Runtime readiness research](runtime-readiness-research.md) — evidence inventory
