# Task #8E — Bounded Offline Local-Player Semantic Correlation

Research only. **GTA remained closed** for this task. No live process access, no registry
changes, no readiness implementation.

## PE / build identity

| Field | Value |
| --- | --- |
| Executable | `SanAndreas.exe` |
| SHA-256 | `63e633bb51c35fe35ca98f503b62e7ddb8403abe4c9d501cfa9162892453846f` |
| Image size | 89,497,064 bytes |
| SHA verified offline | **Yes** |
| Analysis tool | `DeFlux_TestClient.exe --local-player-semantic-offline` |
| Implementation | `local_player_semantic_research.hpp` / `.cpp` |

## Prior context (closed paths)

- Task #7 GWorld research: **CLOSED** — no GWorld-qualified candidate.
- Task #8D live: **`LEGACY_CHAIN_INSUFFICIENT`** — opaque-root → legacy `kProfiles` path
  **CLOSED** for this build.
- Task #8E must **not** modify profile offsets, search new GWorld roots, or reuse legacy
  `kProfiles` chains.

## Analysis method and bounds

Bounded offline PE analysis on the mapped executable image only:

| Bound | Limit |
| --- | --- |
| Direct code xrefs per anchor | 64 |
| Containing `.pdata` functions per anchor | 64 |
| Code window per function | 0x1000 bytes |
| Caller/callee expansion | 1 layer (static, when derivable) |
| RIP-relative global refs per anchor | 128 |
| Call-graph traversal | None (no recursion) |

Xref detection: executable sections, RIP-relative `LEA`/`MOV` with modrm `0x05` pattern
(reuses `decode_x64_rip_relative_lea_or_mov` from GWorld identity analysis).

**Truncation:** No anchor hit xref, function, or global caps on this build.

## Semantic anchors — string locations and counts

Strings located in `.rdata` via exact ASCII scan. Counts match Task #8C inventory.

### AcknowledgedPawn (priority 1)

| String RVA | Section |
| --- | --- |
| `0x04B0A360` | `.rdata` |

Occurrences: **1**. Direct code xrefs: **0**. `.pdata` functions: **0**.

### LocalPlayer (priority 2)

| String RVA | Section |
| --- | --- |
| `0x042BB8C0` | `.rdata` |
| `0x046F4BA0` | `.rdata` |
| `0x046F9F99` | `.rdata` |
| `0x046FE778` | `.rdata` |
| `0x04A18DAA` | `.rdata` |
| `0x04A2CF88` | `.rdata` |
| `0x04A2CFA0` | `.rdata` |
| `0x04A41920` | `.rdata` |
| `0x04B09E3B` | `.rdata` |
| `0x04B718B3` | `.rdata` |
| `0x04B71903` | `.rdata` |

Occurrences: **11**. Direct code xrefs: **0**. `.pdata` functions: **0**.

### APlayerController (priority 3)

| String RVA | Section |
| --- | --- |
| `0x04226158` | `.rdata` |

Occurrences: **1**. Direct code xrefs: **1**. `.pdata` functions: **1**.

| Xref instruction RVA | Target string RVA |
| --- | --- |
| `0x00BE678E` | `0x04226158` |

### PlayerController (priority 4)

22 string occurrences (includes `0x04226159`, one byte after `APlayerController` at
`0x04226158`, plus 21 additional `.rdata` literals). Direct code xrefs: **2** (both target
`0x0497FD20`, not the `APlayerController` substring).

| Xref instruction RVA | Target string RVA |
| --- | --- |
| `0x03522304` | `0x0497FD20` |
| `0x03522AA7` | `0x0497FD20` |

### OwningGameInstance (priority 5)

| String RVA | Section |
| --- | --- |
| `0x04A2C128` | `.rdata` |

Occurrences: **1**. Direct code xrefs: **0**. `.pdata` functions: **0**.

### PersistentLevel (secondary context only)

| String RVA | Section |
| --- | --- |
| `0x0447B288` | `.rdata` |

Occurrences: **1**. Direct code xrefs: **1**. `.pdata` functions: **1**.

| Xref instruction RVA | Target string RVA |
| --- | --- |
| `0x01A7AFCE` | `0x0447B288` |

Not used to restart actor scanning per task bounds.

---

## Containing `.pdata` functions (SEMANTIC_FUNCTION_CANDIDATE)

No function references more than **one** priority semantic anchor. Relationship graph
is **empty** (zero `RELATIONSHIP_CANDIDATE` records with ≥2 priority anchors).

### APlayerController — function `0x00BE66C0`

| Field | Value |
| --- | --- |
| Classification | **SEMANTIC_EVIDENCE_WEAK** |
| Anchors in window | `[APlayerController]` only |
| Provenance | `.pdata` boundary + direct string xref @ `0x00BE678E` |
| Rationale | Single-anchor reflection/registration-style function; no corroborating
  LocalPlayer / AcknowledgedPawn / OwningGameInstance references |

### PlayerController — function `0x035222F0`

| Field | Value |
| --- | --- |
| Classification | **SEMANTIC_EVIDENCE_WEAK** |
| Anchors in window | `[PlayerController]` only |
| Xref | `0x03522304` → string `0x0497FD20` |
| Provenance | `.pdata` + direct xref |

### PlayerController — function `0x03522AA7`

| Field | Value |
| --- | --- |
| Classification | **SEMANTIC_EVIDENCE_WEAK** |
| Anchors in window | `[PlayerController]` only |
| Note | Xref instruction RVA equals function begin (likely mid-instruction `.pdata`
  boundary alignment); treated as weak single-anchor evidence only |

### PersistentLevel — function `0x01A799D0` (secondary)

| Field | Value |
| --- | --- |
| Classification | **SEMANTIC_EVIDENCE_WEAK** |
| Anchors in window | `[PersistentLevel]` only |
| Xref | `0x01A7AFCE` |
| Note | Large bounded window with many rodata address-taken globals; consistent with
  level/type registration, not local-player retrieval |

---

## Bounded relationship graph

```
AcknowledgedPawn (0 xrefs)     [isolated — no code correlation]
LocalPlayer (0 xrefs)          [isolated — no code correlation]
OwningGameInstance (0 xrefs)   [isolated — no code correlation]

APlayerController ──xref──► fn 0x00BE66C0     (single anchor)
PlayerController  ──xref──► fn 0x035222F0     (single anchor)
PlayerController  ──xref──► fn 0x03522AA7     (single anchor)
PersistentLevel   ──xref──► fn 0x01A799D0     (secondary; not expanded)
```

**No edges** connect priority anchors to each other within the same `.pdata` function.
**No** statically derivable one-layer caller/callee shared multi-anchor functions were
found within bounds.

---

## FIELD_OFFSET_CANDIDATE (selected; not identity-qualified)

All offsets below are **shape-only** static disp32 accesses in semantic function windows.
They are **not** UE field names.

### APlayerController function `0x00BE66C0`

| Instr RVA | Access | Offset | Base reg | Notes |
| --- | --- | --- | --- | --- |
| `0x00BE6719` | read | `0x298` | r11 | Competing: generic UObject member |
| `0x00BE6732` | address_calc | `0x30` | r8 | Competing: vtable/interface slot |
| `0x00BE6746` | read | `0x30` | r9 | Same |
| `0x00BE6795` | address_calc | `0x20` | r12 | Competing: unrelated struct field |
| `0x00BE6801` | read | `0x848` | r12 | Large offset — likely nested subobject |

Classification: **SEMANTIC_EVIDENCE_PROMISING_UNVERIFIED** (offset shape near semantic
function; anchor relationship unproven).

### PlayerController function `0x035222F0`

| Instr RVA | Access | Offset | Base reg |
| --- | --- | --- | --- |
| `0x0352235F` | write | `0x428` | rdi |
| `0x03522365` | read | `0xC` | rdi |
| `0x0352239C` | read | `0x8` | rbp |
| `0x035223C6` | read | `0xE8` | rdi |

Classification: **SEMANTIC_EVIDENCE_WEAK** — registration/logging pattern; string at
`0x0497FD20` may be format/diagnostic text, not runtime PC identity.

PersistentLevel function `0x01A799D0` produced **>100** disp32 candidates in the first
0x1000 window (mostly repeated `+0x30` writes and large rodata struct initialization).
Not tabulated exhaustively — **not** local-player evidence.

---

## SEMANTIC_GLOBAL_CANDIDATE

Globals recorded only where RIP-relative read/write/LEA appeared in anchor-associated
functions within bounds.

### Cross-path note (not independent identity proof)

| Global RVA | Section | Access | Associated function | Anchor context |
| --- | --- | --- | --- | --- |
| `0x05764130` | `.data` | read | `0x01A799D0` | PersistentLevel (secondary) |

This is the same opaque research slot exercised in Task #8D (legacy chain). Task #8D
live showed **`LOCAL_PLAYERS_BAD`** / no transform for all profiles. Referencing it from
a PersistentLevel registration function does **not** revive the closed legacy chain and
does **not** justify a new local-player live experiment.

### APlayerController function `0x00BE66C0`

| Global RVA | Section | Access |
| --- | --- | --- |
| `0x050250E8` | `.data` | read |
| `0x05725750` | `.data` | read |

Classification: **SEMANTIC_EVIDENCE_PROMISING_UNVERIFIED** — `.data` globals read from
single-anchor function; identity and cross-phase stability unproven.

### PlayerController functions

| Global RVA | Section | Access | Functions |
| --- | --- | --- | --- |
| `0x05087380` | `.data` | read | `0x035222F0` |

No global appears in **both** an APlayerController-path function and a
PlayerController-path function with clear read semantics.

---

## Evidence classifications (summary)

| Candidate | Classification |
| --- | --- |
| AcknowledgedPawn string presence | **SEMANTIC_EVIDENCE_WEAK** (metadata only; 0 code xrefs) |
| LocalPlayer string presence (×11) | **SEMANTIC_EVIDENCE_WEAK** (metadata only; 0 code xrefs) |
| OwningGameInstance string presence | **SEMANTIC_EVIDENCE_WEAK** (metadata only; 0 code xrefs) |
| APlayerController xref → fn `0x00BE66C0` | **SEMANTIC_EVIDENCE_WEAK** |
| PlayerController xrefs → fns `0x035222F0` / `0x03522AA7` | **SEMANTIC_EVIDENCE_WEAK** |
| Multi-anchor function correlation | **SEMANTIC_EVIDENCE_CONTRADICTED** (expected for UE
  player graph; **absent** in bounded static analysis) |
| PersistentLevel fn + global `0x05764130` | **SEMANTIC_EVIDENCE_CONTRADICTED** for local-player
  identity (reinforces #8D chain failure; level registration ≠ local player) |
| Legacy kProfiles chain | **SEMANTIC_EVIDENCE_CONTRADICTED** (Task #8D live; path closed) |

**SEMANTIC_EVIDENCE_STRONG:** None. Strong classification requires multi-priority-anchor
correlation in one function; **zero** instances found.

---

## Ambiguities and contradictions

1. **Primary anchors without code xrefs:** `AcknowledgedPawn`, all `LocalPlayer` literals,
   and `OwningGameInstance` appear only as `.rdata` strings with **no** bounded
   RIP-relative code references. Static correlation toward local-player identity **cannot**
   start from these anchors on this build.

2. **PlayerController substring overlap:** `0x04226159` is the `PlayerController` suffix of
   `APlayerController` at `0x04226158`. The one `APlayerController` xref targets the full
   string; the two `PlayerController` xrefs target a **different** literal (`0x0497FD20`).
   No static proof links these into one player-controller identity chain.

3. **PersistentLevel ↔ `0x05764130`:** Secondary function reads the Task #8D opaque slot,
   but live diagnostic proved that slot does not yield legacy local-player chains. Using
   this for actor/local-player discovery is **explicitly out of scope** and contradicted
   by live evidence.

4. **Field offsets near semantic functions:** Repeated `+0x30`, `+0x298`, etc. match common
   UE object layouts but are **indistinguishable** from unrelated engine types without
   runtime identity proof — reported as `FIELD_OFFSET_CANDIDATE` only.

---

## Truncation / bounds

| Anchor | Xrefs truncated | Functions truncated | Globals truncated |
| --- | --- | --- | --- |
| All | No | No | No |

All xref/function counts are well below per-anchor caps (max xrefs observed: 2).

---

## Justified live candidate?

**No — `NO JUSTIFIED LIVE CANDIDATE`.**

Offline analysis failed to produce any `.pdata` function referencing two or more priority
player-related semantic anchors. Priority anchors #1–#2 (`AcknowledgedPawn`, `LocalPlayer`)
have **zero** code xrefs. Task #8D closed the only previously attempted pointer chain.

Manufacturing a live experiment from isolated single-anchor functions (e.g. reading
`0x050250E8` from `0x00BE66C0`) would violate anti-guessing rules: no independent static
correlation to local-player identity, no falsifiable hypothesis tied to multi-anchor
evidence.

### Reason research is blocked

Bounded static semantic correlation on the exact build does **not** justify **one** future
read-only local-player identity experiment. Next progress requires **new independent
evidence** (e.g. a different static derivation path, or a non-legacy runtime observation
design explicitly authorized by architect review) — not an extension of closed Task #7/#8D
chains.

---

## Platform state (unchanged)

| Field | Value |
| --- | --- |
| Supported registry | **EMPTY** |
| Compatibility | **DETECTED_BUT_UNSUPPORTED** |
| Readiness | **NOT_IMPLEMENTED** |
| GTA in Task #8E | **CLOSED** |

---

## Task #8 boundary (semantic research)

| Label | This task |
| --- | --- |
| **OBSERVED FACT** | String counts, xref counts, function RVAs from deterministic offline PE scan |
| **RESEARCH CANDIDATE** | Field offsets, globals, single-anchor functions — all unverified |
| **ARCHITECTURAL CONCLUSION** | `NO JUSTIFIED LIVE CANDIDATE`; do not continue arbitrary string/xref/global chasing |
| **CLOSED PATH** | Task #7 opaque roots + legacy kProfiles (Task #8D); not reopened here |

See [runtime-readiness-research.md](runtime-readiness-research.md) — **Task #8 Final
Architectural Decision** and **Next Research Direction**.

---

## Related documents

- [Runtime readiness research](runtime-readiness-research.md) — Task #8A–#8E checkpoint
- [GWorld discovery research](gworld-discovery-research.md) — Task #7 closed negative result
