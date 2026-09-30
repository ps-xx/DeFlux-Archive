# GWorld Discovery Research (Tasks #7A–#7B)

Research-only tooling. **Not** invoked by production runtime qualification.

## Purpose

Produce deterministic static and controlled live evidence about GWorld-like global
pointer slots for a specific executable build.

This is a **research program**, not a GWorld qualification pass. **No GWorld
candidate has been validated or promoted.**

## Target build (research baseline)

| Field | Value |
| --- | --- |
| SHA-256 | `63e633bb51c35fe35ca98f503b62e7ddb8403abe4c9d501cfa9162892453846f` |
| Compatibility | `DETECTED_BUT_UNSUPPORTED` |
| Supported-build registry | **EMPTY** |
| Readiness | **NOT_IMPLEMENTED** |

## Final research conclusion

**NO GWORLD-QUALIFIED CANDIDATE EXISTS FOR THE CURRENT BUILD.**

Offline static analysis, offline identity correlation, and controlled live
observation did not produce a defensible GWorld/UWorld identity. This is a valid
**negative research result**.

## Separation from runtime (trust boundary)

| Layer | Role |
| --- | --- |
| `run_gworld_discovery_research()` | Offline structural discovery (Task #7A) |
| `analyze_semantic_candidates()` | Offline semantic discrimination (Task #7A.2) |
| `analyze_identity_evidence()` | Offline UE/world identity correlation (Task #7A.3) |
| `run_live_candidate_observation_session()` | Controlled live slot observation (Task #7B.1) |
| `run_structural_identity_probe()` | Bounded live structural probe (Task #7B.2) |
| `discover_gworld_slots_in_image()` | Legacy broad signature scan (Task #6); regression only |
| `qualify_observation()` | Runtime probe using a **reviewed profile RVA** only |

Production flow remains: fingerprint → reviewed profile → known anchor → fast qualification.

The following research labels are **observational metadata only** and must **not**
register supported builds, promote runtime profiles, enable qualification success,
readiness, injection, hooks, or gameplay changes:

- `GWorld-like`, `IDENTITY_SUPPORTED`, `IDENTITY_AMBIGUOUS`, `IDENTITY_CONTRADICTED`
- `OBSERVATION_CONSISTENT`, `OBSERVATION_INCONSISTENT`, `INSUFFICIENT_DATA`
- `STRUCTURE_OBSERVED`, `STRUCTURE_INSUFFICIENT`, `STRUCTURE_CONTRADICTED`

Normal DeFlux runtime must **not** perform broad research scans or live candidate
observation. Research paths are exposed only via explicit `DeFlux_TestClient.exe`
flags (see [Invocation](#invocation)).

## Safety boundaries (Tasks #7B.1 / #7B.2)

Live research uses **read-only** process access only:

| Allowed | Forbidden |
| --- | --- |
| `PROCESS_QUERY_LIMITED_INFORMATION` | `PROCESS_VM_WRITE`, `PROCESS_VM_OPERATION` |
| `PROCESS_VM_READ` | Injection, DLL loading, hooks, remote threads |
| `ReadProcessMemory` | Patching, `VirtualProtect`, gameplay manipulation |
| Task #4 verified process identity | Launching GTA from the tool |
| SHA-256 re-verification on disk | GTA / Rockstar file modification |

Memory walk limits (Task #7B.2 structural probe):

- Global slot → candidate object pointer → **256-byte object prefix** (32 pointer-sized fields)
- **At most one** additional pointer-sized read per canonical field (no recursion)
- Structural probing allowed **only** for `0x05764130`

The diagnostic tool **never launches GTA**. The operator must start vanilla GTA
manually through Steam.

---

## Task #7A — Controlled structural discovery

Pipeline (deterministic, offline, read-only):

1. Decode `mov reg, [rip+disp32]` (REX.W `48/4C 8B`) in executable sections only.
2. Require slot within image, 8-byte aligned, in writable pointer-storage sections.
3. Reject import directory RVAs.
4. Require post-load `test reg, reg` within bounded context.
5. Require ≥12 converging code references to the same slot.
6. Cap shortlist at 20; abort if still too many.

### Results (target build)

| Stage | Count |
| --- | --- |
| Raw recognized references | 195,283 |
| After context filters | 72,030 |
| Converged research candidates (≥12 refs) | 86 |
| Final shortlist returned | **0** |

**Outcome:** static structural discrimination was insufficient.

## Task #7A.2 — Semantic discrimination

| Classification | Count |
| --- | --- |
| Analyzed (converged dataset) | 86 |
| GWorld-like | 39 |
| Ambiguous | 6 |
| Unlikely | 41 |

**Outcome:** 39 GWorld-like candidates exceeded the review threshold (20).

## Task #7A.3 — UE-specific offline identity evidence

| Classification | Count |
| --- | --- |
| IDENTITY_SUPPORTED | 1 |
| IDENTITY_AMBIGUOUS | 38 |
| IDENTITY_CONTRADICTED | 0 |

The sole `IDENTITY_SUPPORTED` result (`0x05764130`) had **WEAK / MIXED /
INSUFFICIENT** offline evidence (one `PersistentLevel` xref mixed with
`RenderThread` in the same function). Popularity is not identity evidence.

**Outcome:** offline static analysis did not reliably identify GWorld.

---

## Task #7B.1 — Controlled live candidate observation

Methodology:

- Architect-approved probe set: `0x05391B70`, `0x05764130` only
- Task #4 verified process identity; SHA-256 re-verification; ASLR module resolution
- Read-only slot sampling with operator-supplied phase labels
- Default 500 ms interval; bounded sample count; `PROCESS_CHANGED` abort

Phase labels (observational metadata only): `MENU`, `LOADING`, `GAMEPLAY_STANDING`,
`GAMEPLAY_MOVING`, `PAUSE_MENU`.

### Live observation — two independent sessions

#### `0x05391B70` — historical / legacy only

| Phase | Observed slot value |
| --- | --- |
| MENU | non-null |
| LOADING | non-null |
| GAMEPLAY (and later phases) | **NULL** |

Live evidence does **not** support promotion. Retain as historical research only.
Trust status remains **LEGACY_CANDIDATE_UNVERIFIED**. **NOT GWorld-qualified.**

#### `0x05764130` — active live research candidate (not qualified)

| Evidence | Observation |
| --- | --- |
| MENU, LOADING, GAMEPLAY_STANDING, GAMEPLAY_MOVING, PAUSE_MENU | stable non-null across **two** independent sessions |
| Process lifetime | object address stable within session; **changes across process restarts** |
| First pointer-sized value | consistently points inside loaded game module |

Live observation alone does **not** establish GWorld identity. **NOT GWorld-qualified.**

Research labels (`OBSERVATION_CONSISTENT`, etc.) describe sampling stability only.

---

## Task #7B.2 — Bounded structural identity probe

Methodology:

- **Only** `0x05764130` may be structurally traversed (`0x05391B70` rejected)
- 256-byte object prefix; 32 eight-byte fields (`0x00`–`0xF8`)
- One optional dereference per canonical pointer field; no deeper reads
- Classifications: `STRUCTURE_OBSERVED`, `STRUCTURE_INSUFFICIENT`, `STRUCTURE_CONTRADICTED`

### Structural observation (`0x05764130`)

| Evidence | Result |
| --- | --- |
| Bounded 256-byte prefix read | **Succeeded** |
| First qword | points inside loaded game module (`STRUCTURE_OBSERVED`) |
| Prefix pattern | highly regular pointer/scalar layout observed |
| UE object identity | **unproven** — no demonstrated field names or layout facts |

`HYPOTHESIS_ONLY` notes may describe vtable-like first-qword patterns. These are
not demonstrated identity. Offline Task #7A.3 evidence remains **WEAK / MIXED /
INSUFFICIENT**.

**Do not assert what `0x05764130` represents.** Allocator/container/registry
interpretations are architect observations only, not code-demonstrated identity.

---

## Task #7B.2A — Structural probe reporting fix

Presentation-only fix: CLI now prints **all 32** captured field rows (`0x00` through
`0xF8`) via `format_structural_identity_field_lines()`. Probe semantics unchanged.

---

## Key slot dispositions (final)

### `0x05764130` — NOT GWorld-qualified

- High-traffic offline candidate; sole weak offline `IDENTITY_SUPPORTED` result
- Stable non-null live observation across five phase labels (two sessions)
- Bounded structural snapshot succeeded; first qword module-resident
- Structural identity **unproven**; offline identity **insufficient**
- **No RVA promotion**

### `0x05391B70` — LEGACY_CANDIDATE_UNVERIFIED (historical)

- Non-null during MENU/LOADING; NULL during observed gameplay phases
- Live evidence does not support promotion
- Historical research record only; not structurally traversed by Task #7B.2
- **NOT GWorld-qualified**

### `0x0FE1D1D8` — excluded

Outside `SizeOfImage` for this build. Zero references.

---

## Invocation

```text
DeFlux_TestClient.exe --gworld-research-offline [path-to-SanAndreas.exe]
DeFlux_TestClient.exe --gworld-observe-live [--max-samples N] [--interval-ms MS]
DeFlux_TestClient.exe --gworld-structural-probe
```

All live paths require a manually launched verified GTA process. Tools never launch
GTA. SHA-256 must match the research baseline hash.

---

## Regression tests

Foundation tests cover offline discovery, semantic/identity analysis, live
observation logic, structural probe bounds, and field-report formatting.

Foundation baseline: **419 passed, 0 failed** (post Task #7B.2A).
