# Task #9 — In-Process Research Architecture & Activation Pipeline

The legacy DeFluxCore client referenced by historical research notes is intentionally excluded from this archival snapshot.

**Status: Task #9B CLOSED.**

| Boundary | Result |
| --- | --- |
| Activation pipeline | **VALIDATED** |
| Lifecycle fix | **VALIDATED LIVE** (`LIVE_LIFECYCLE_FIX_VALIDATED`) |
| gGameState semantics | **INSUFFICIENT** (`IN_PROCESS_SIGNAL_INSUFFICIENT`) |
| Production supported-build registry | **EMPTY** |
| Compatibility | **DETECTED_BUT_UNSUPPORTED** |
| Readiness | **NOT_IMPLEMENTED** |
| Local-player identity | **NOT IMPLEMENTED / NOT ESTABLISHED** |
| GWorld | **NO QUALIFIED CANDIDATE** |

Closing checkpoint before this finalization: `68236d2423ee757f3875481ffd7bbd0126d47ccb`.

This close validates only the tested bounded **PASSIVE_READ** activation pipeline for
`GTA_SA_DE_GGAMESTATE_OBSERVATION_V1`. It does not authorize arbitrary in-process code,
arbitrary GTA memory access, hooks, engine calls, writes, local-player access, or world
readiness. Task #10 is not started by this document.

**Prior design checkpoint:** `5b71b61afb1a5272d80e2479b491f07a7c595f35` (Task #8 closed)

**Target build (research allowlist only, not production-supported):**
`63e633bb51c35fe35ca98f503b62e7ddb8403abe4c9d501cfa9162892453846f`

### Pre-live checkpoint — OBSERVED vs NOT PROVEN

Task #9B Phase 1A proves the activation **PIPELINE** on a DeFlux-owned synthetic target.
It does **NOT** prove GTA semantic behavior.

**OBSERVED (synthetic E2E + harness stress in this tree):**

- synthetic remote `LoadLibrary` pipeline succeeded
- deferred bootstrap completed in the fixture
- intent Pending → Consumed succeeded
- bounded observation produced evidence (`SESSION_START` / `SAMPLE` / `SESSION_END`)
- automatic STOP completed
- 100× lifecycle stress passed in this run (auto-stop, explicit shutdown, failure cleanup)

**NOT PROVEN:**

- universal `DllMain` / loader-lock safety
- live GTA activation
- GTA async sampler behavior
- `gGameState` semantic identity beyond Task #8B
- world readiness
- local-player identity

Production **Supported registry remains EMPTY**. Research allowlist remains separate.
Normal `--inject` remains restricted to a verified GTA SA DE target; synthetic seams are
test-only and cannot weaken or bypass the normal live gate.

---

## 1. Purpose

Task #8 exhausted **external** evidence strategies for local-player identity on this build.
Task #9A designed the **smallest safe in-process research architecture**; Task #9B Gate 1–2B
Phase 1A implements the activation pipeline and proves it on a **synthetic** fixture only.

This document does **not**:

- authorize live GTA experiments (Phase 2 requires separate architect approval)
- add the build to the production **Supported registry**
- implement readiness or local-player discovery
- prescribe hooks, engine calls, or persistent GTA directory artifacts
- claim universal loader-lock safety beyond what synthetic E2E demonstrated

It **does** define bootstrap, boundaries, lifecycle, capabilities, evidence rules, and the
synthetic pipeline proof so live Phase 2 can be reviewed before any GTA injection.

---

## 2. Existing infrastructure audit

### 2.1 Classification table

| Mechanism | Path | Classification | Rationale |
| --- | --- | --- | --- |
| Runtime Host C ABI | `runtime/include/deflux/runtime/abi.h`, `host.cpp` | **REUSE_AS_IS** | Correct minimal lifecycle; no GTA coupling |
| Runtime minimal DllMain | `runtime/src/dllmain.cpp` | **REUSE_AS_IS** | Loader-lock safe; explicit initialize only |
| Runtime Harness | `tests/runtime/src/main.cpp` | **REUSE_WITH_HARDENING** | Extend for future research module load tests; still same-process only |
| Foundation process lifecycle | `foundation/include/deflux/core/process_lifecycle.hpp` | **REUSE_AS_IS** | Independent machine; external observation unchanged |
| `IGameAdapter` contract | `foundation/include/deflux/game/adapter.hpp` | **REUSE_AS_IS** | Mandatory boundary; readiness stubs correct |
| `GtaSaDeAdapter` (external) | `game_adapters/gta_sa_de/` | **REUSE_WITH_HARDENING** | Split external vs future in-process research surfaces |
| Fingerprint / SHA-256 | `fingerprint.cpp` | **REUSE_AS_IS** | Exact-build gate foundation |
| Supported-build registry | `supported_builds.cpp` (`kSupportedSha256{}` empty) | **REUSE_AS_IS** | Must remain separate from research allowlist |
| Environment preflight | `environment_preflight.cpp` | **REUSE_WITH_HARDENING** | Extend to detect in-process research artifacts if any |
| M1 runtime directory guard | `foundation/src/core/m1_runtime_directory.cpp` | **REUSE_AS_IS** | Prevents VERSION.dll proxy contamination of M1 tools |
| Module query + pointer validation | `module_query.cpp`, `qualification.hpp` helpers | **REUSE_WITH_HARDENING** | Adapt for in-process module self-query |
| Remote read (`ScopedProcess`) | `remote_read.cpp` | **REUSE_AS_IS** (external path) | External research continues unchanged |
| Task #8 research modules | `ggame_state_observation.*`, etc. | **REUSE_AS_IS** (external) | Remain CLI-only; not auto-wired to runtime |
| TestClient research CLI | `test_client/src/main.cpp` | **REUSE_WITH_HARDENING** | Future control channel client; external only |
| Legacy `version.dll` proxy | `launcher/version_proxy/` | **REJECT** | Persistent hijack; loads DeFluxCore; M1 preflight contaminant |
| Legacy `DeFluxCore` / `dllmain.cpp` | `client/src/dllmain.cpp` | **REJECT** | Hooks, writes, menu manipulation, unverified GWorld chain |
| Legacy MinHook / hook engine | `client/src/hooks/hook_engine.cpp` | **REJECT** | Software tick + stub Present; wrong model for research gen 1 |
| Legacy menu boot / inline patch | `sa_interface_boot.cpp` | **REJECT** | Code modification; Story Mode flow manipulation |
| Legacy `ue4_memory.cpp` | `client/src/game/ue4_memory.cpp` | **RESEARCH_REFERENCE_ONLY** | Chain structure reference; paths closed for this build |
| Legacy `local_player.cpp` | `client/src/game/local_player.cpp` | **REJECT** | Fallback coordinates; identity overclaim |
| Legacy `ue4_gameplay_gate.cpp` | `client/src/ue4/ue4_gameplay_gate.cpp` | **RESEARCH_REFERENCE_ONLY** | gGameState timing semantics validated externally (#8B) |
| Legacy SP suppression / teleports | `sp_script_guard.cpp`, write paths | **REJECT** | Modification-only |

### 2.2 Current architecture gap

```text
Today (M1):
  TestClient ──► GtaSaDeAdapter (static) ──► OpenProcess + RPM (external)

  DeFlux_Runtime_Harness ──► LoadLibrary(DeFlux_Runtime.dll) ──► stub host only
                             (same process as harness, NOT GTA)

Missing (future):
  Research Launcher ──► explicit in-process load ──► DeFlux_Runtime.dll
                             └──► In-Process Game Adapter Research Module
```

The Runtime Host **does not** load the Game Adapter today. In-process research requires a
**new, research-only** module below the host — not a port of legacy `DeFluxCore`.

---

## 3. Chosen future bootstrap architecture

### 3.1 Preferred: **Explicit Research Launcher (one-shot injection)**

A dedicated **`DeFlux_ResearchLauncher`** (future executable, M1 output directory):

1. Operator starts **vanilla GTA** manually (Steam) with **clean** Win64 directory (preflight PASS).
2. Operator runs launcher from M1 build dir with explicit flags, e.g.:
   `--experiment <id> --passive-read --max-samples N --output <path>`
3. Launcher verifies:
   - target process identity (PID + creation FILETIME + verified exe path)
   - SHA-256 matches **research allowlist** entry (not supported registry)
   - environment preflight PASS
   - research capability = `PASSIVE_READ` only
4. Launcher performs **one explicit** `LoadLibraryW` of `DeFlux_Runtime.dll` (absolute path
   under M1 build dir) into the GTA process via a documented injection primitive
   (e.g. `CreateRemoteThread` + `LoadLibraryW` — implementation deferred to #9B+).
5. Loaded runtime receives init context including experiment ID, output path, capability mask.
6. On shutdown or process exit, launcher verifies **no persistent artifacts** were written to
   Win64 (except explicit user-configured log path outside game dir).

**Why this wins:**

| Criterion | One-shot launcher |
| --- | --- |
| Deterministic | User-initiated; logged steps |
| Explicit | No silent proxy; operator command required |
| Reversible | No files left in Win64; DLL unloaded on shutdown |
| Isolated | Research DLL lives in M1 dir only |
| Minimal GTA interference | No menu hooks; PASSIVE_READ only |
| Easy to disable | Don't run launcher |
| Testable without GTA | Launcher logic unit-tested; injection mocked |
| Clean launcher architecture | Aligns with future DeFlux Launcher owning attach policy |
| No legacy proxy | Does not use `version.dll` hijack |

### 3.2 Runtime initialization contract (extension, future ABI revision)

Extend `DefluxRuntimeInitContext` in a **new ABI version** when implementing (not in #9A):

- `research_flags` (disabled by default)
- `experiment_id` (numeric / four-cc)
- `capability_mask` (`PASSIVE_READ` only for gen 1)
- `output_channel` descriptor (pipe name or log path — caller-owned string in shared mapping)

M1 `flags` remain `0`; research inactive unless future flag set **and** research allowlist matches.

---

## 4. Rejected bootstrap alternatives

| Alternative | Verdict | Reason |
| --- | --- | --- |
| Legacy `version.dll` proxy | **REJECT** | Persistent Win64 artifact; hijacks `VERSION.dll` imports (breaks M1 tools if misplaced); delayed auto-load of DeFluxCore; loads all `scripts\*.asi`; not explicit |
| Persistent research DLL in Win64 | **REJECT** | Violates vanilla isolation; recurrence of contamination incident |
| `dinput8.dll` / `dxgi.dll` proxy | **REJECT** | Same persistence/hijack class as version proxy |
| ASI loader + research ASI beside game | **REJECT** | Persistent game-dir artifact; couples to third-party ASI load order |
| Manual copy of runtime into Win64 | **REJECT** | Silent persistence; operator error prone |
| Inject legacy `DeFluxCore.asi` for research | **REJECT** | Hooks, writes, menu boot, closed GWorld paths |
| External RPM only (status quo) | **REJECT as next strategy** | Task #8 exhausted external identity evidence; remain available but not expansion path |
| Implicit auto-inject on GTA detect | **REJECT** | Not explicit; normal GTA launch must stay vanilla |

---

## 5. Research module boundary

### 5.1 Layering (mandatory)

```text
DeFlux Runtime Host          (runtime/ — lifecycle, ABI, fault envelope)
        |
        v
In-Process Game Adapter       (future — game-specific, inside GTA process)
        |
        v
Research Observation API    (research-only interface)
        |
        v
Experiment implementation   (Task #9B+ — one experiment per module/version)
```

Core DeFlux / foundation **must not** include UE4 offsets, GWorld slots, or GTA memory layouts.

### 5.2 Research Observation API (conceptual)

Research-only C++ interface inside the game adapter research partition:

| Operation | Responsibility |
| --- | --- |
| `query_research_status` | Experiment id, state, capability, last error |
| `arm_experiment` | Validate allowlist + capability; transition DISABLED→ARMED |
| `begin_observation` | ARMED→OBSERVING; start sample loop policy |
| `sample` | One bounded observation tick; append structured record |
| `stop_observation` | OBSERVING→STOPPING→STOPPED |
| `shutdown_research` | Release buffers; idempotent |

**Not** exposed through public `IGameAdapter` readiness methods.

**Not** automatically invoked from `deflux_runtime_initialize` in M1/gen-1.

Experiments register by ID; host refuses unknown IDs.

---

## 6. Lifecycle model

### 6.1 Runtime Host (existing — do not duplicate)

Uses `DefluxRuntimeHostState` only: `UNINITIALIZED` → `INITIALIZING` → `INITIALIZED` →
`SHUTTING_DOWN` → `STOPPED` / `FAILED`.

Research substate is **orthogonal** — host may be `INITIALIZED` while research is `DISABLED`.

### 6.2 Research substate (minimum model)

| State | Meaning |
| --- | --- |
| `DISABLED` | Default after runtime init; no experiment armed |
| `ARMED` | Experiment ID + capability validated; awaiting begin |
| `OBSERVING` | Sample loop active |
| `STOPPING` | Draining samples; flushing output |
| `STOPPED` | Clean completion; may re-arm only after explicit arm |
| `FAILED` | Unrecoverable error; requires shutdown/re-init |

### 6.3 Transition rules

| Event | Behavior |
| --- | --- |
| Runtime initialize success | Research = `DISABLED` |
| Unsupported / wrong SHA | `arm_experiment` → `FAILED`; no samples |
| Unknown build (fingerprint fail) | `FAILED`; fail closed |
| Environment contaminated | `FAILED`; no attach (launcher should pre-check) |
| Experiment disabled (default) | All sample APIs return `NOT_ARMED` |
| Experiment start | `ARMED` → `OBSERVING` after double-check allowlist |
| Sample failure (bad pointer) | Record failure; increment counter; continue if under limit |
| Repeated sample failures (>N consecutive) | → `FAILED`; stop observation |
| Shutdown requested | `STOPPING` → flush records → `STOPPED` |
| GTA process exit | DLL detach; best-effort flush; no host crash |
| Runtime shutdown | Force research `STOPPING` → `STOPPED` before host `SHUTTING_DOWN` |
| SEH fault in sample | Contain; mark sample failed; count toward abort threshold |

Research failure **must not** terminate the Runtime Host abnormally if containment succeeds;
host reports `FAILED` research substate in status diagnostic.

---

## 7. Threading model

### 7.1 First generation (Task #9B): **Dedicated research sampling thread**

| Thread | Role |
| --- | --- |
| Game / render threads | **Untouched** — no hooks, no callbacks installed |
| Runtime Host | Init/shutdown on launcher/injection thread |
| Research sampler | Explicit worker thread at fixed interval (e.g. 500 ms) |

**Rationale:** Avoids hooking Present/Tick; matches Task #8 external sampling cadence; easier
to disable; clear ownership.

### 7.2 Thread-safety rules

| Access class | Gen-1 rule |
| --- | --- |
| Raw bytes at **pre-derived fixed address** (e.g. gGameState global) | Allowed from sampler thread with SEH + bounded read |
| Pointer chasing beyond approved derivation | Not in gen-1 without architect approval |
| Engine virtual calls / UObject methods | **Forbidden** — may require game thread (future, separate approval) |
| Writes | **Forbidden** all generations until new capability tier approved |

Document distinction:

1. **Passive memory observation** — bounded, SEH-wrapped reads from research thread.
2. **Engine interaction** — deferred; would require game-thread dispatch architecture (not #9A).

---

## 8. Capability model

### 8.1 Tiers

| Capability | Gen-1 | Description |
| --- | --- | --- |
| `PASSIVE_READ` | **Allowed** | Bounded `memcpy`/SEH read of pre-approved addresses in current process |
| `SCAN_READ` | Forbidden | Range/heap/module scans |
| `INVOKE` | Forbidden | Engine/native function calls |
| `WRITE` | Forbidden | Any memory modification |
| `HOOK` / `DETOUR` | Forbidden | Code patching, MinHook, trampolines |
| `SPAWN` / `TELEPORT` / `SCRIPT` | Forbidden | Gameplay manipulation |

### 8.2 Expansion policy

New capabilities require:

1. Architecture doc amendment
2. Explicit architect approval
3. Separate experiment class + tests
4. Never implied by `PASSIVE_READ`

Launcher and runtime **fail closed** if requested capability mask includes forbidden bits.

---

## 9. Exact-build research gating

### 9.1 Two registries (must stay separate)

| Registry | Purpose | Current state |
| --- | --- | --- |
| `SUPPORTED_BUILD_REGISTRY` | Production compatibility / support promise | **EMPTY** |
| `RESEARCH_BUILD_ALLOWLIST` | Experiments may collect bounded evidence on exact SHA | Future: explicit research-only entries |

Research allowlist entry (future shape):

```text
sha256, experiment_id[], max_capability, notes, task_authorization_ref
```

Allowlist match means: *"This experiment may run on this executable."*

It does **not** mean: *"DeFlux supports this GTA build."*

### 9.2 Fail-closed gates (in order)

1. Environment preflight clean (no legacy proxy / DeFluxCore in Win64)
2. Verified process image path + creation identity
3. Module base/size match expected main module
4. SHA-256 == allowlisted research entry
5. Experiment ID authorized for that SHA
6. Capability mask ⊆ permitted for experiment
7. Runtime Host `INITIALIZED`

Wrong SHA → **reject** (not downgrade to external).

---

## 10. Observation / evidence model

### 10.1 Structured record (every sample)

| Field | Required |
| --- | --- |
| `schema_version` | Yes |
| `executable_sha256` | Yes |
| `module_path` | Yes |
| `module_base` | Yes |
| `process_id` | Yes |
| `creation_identity` | Yes |
| `session_uuid` | Yes (per attach) |
| `experiment_id` | Yes |
| `experiment_version` | Yes |
| `sample_index` | Yes |
| `monotonic_time_ms` | Yes (QPC or steady clock) |
| `capability` | Yes |
| `derivation_provenance` | Yes (human-readable + machine enum) |
| `observation_result` | `OK` / `READ_FAILED` / `VALIDATION_FAILED` / etc. |
| `payload` | Typed per experiment (bounded size) |
| `failure_reason` | If not OK |
| `classification` | e.g. `OBSERVED` / `INSUFFICIENT` / `CONTRADICTED` |

### 10.2 Output

- **Primary:** append-only structured log (JSON Lines or length-prefixed binary) under user-specified **non-game** directory
- **Secondary:** human-readable summary lines for operator
- No PII; no Documents/Rockstar paths; no save data

Vague stdout-only logging is **insufficient** as sole evidence.

---

## 11. Local-player identity evidence rules

### 11.1 Evidence ladder (terminology)

| Rung | Label | Meaning |
| --- | --- | --- |
| L0 | `STRUCTURAL_EVIDENCE` | Pointer/transform shape, module-local addresses — **not identity** |
| L1 | `BEHAVIORAL_CORRELATION` | Signal co-varies with manual phase labels (e.g. gGameState 7 vs 9) |
| L2 | `INDEPENDENT_SEMANTIC_EVIDENCE` | Multiple independent static/dynamic anchors agree |
| L3 | `IDENTITY_PRESERVED` | L0–L2 satisfied + falsification survived + no contradictory sample |

### 11.2 Maximum conclusions

| Conclusion | Minimum bar |
| --- | --- |
| `LOCAL_PLAYER_CANDIDATE_PRESERVED` | Bounded chain + transform shape + phase correlation; **still not identity** |
| `LOCAL_PLAYER_IDENTIFIED` | L3 + **independent validation** (second evidence class); architect acceptance |

**Never sufficient alone:**

- Single pointer chain
- Plausible FTransform
- Coordinates near Grove Street / CJ spawn
- gGameState == 9
- One semantic string xref

Task #8D/#8E results **revoke** legacy chain and static semantic paths for this build until
new independent evidence appears.

---

## 12. Fault containment (gen-1 minimum)

| Requirement | Detail |
| --- | --- |
| Canonical pointer check | Reuse `is_canonical_user_pointer` (qualification) |
| Module range check | Address in main module or known heap range before dereference |
| Bounded read | Max bytes per read (e.g. 8); max aggregate per sample (e.g. 64) |
| Pointer depth | Gen-1 default: **0** (no chase) or **1** if experiment explicitly approved |
| No recursion | No linked structure walks |
| SEH / equivalent | Wrap each read; count failures |
| Abort threshold | e.g. 10 consecutive read failures → `FAILED` |
| Stale identity | Re-verify PID + creation identity each sample where external launcher coordinates |
| Shutdown | Stop sampler thread with timeout; join before unload |
| Process exit | `DLL_PROCESS_DETACH` → flush + stop threads (no blocking indefinitely) |

No speculative deep traversal in gen-1.

---

## 13. Control / output channel

### 13.1 Preferred: **Named pipe + TestClient controller (local only)**

| Component | Role |
| --- | --- |
| In-process research module | Pipe **server** (`\\.\pipe\DeFluxResearch_<session>`) |
| TestClient (or Research Launcher) | Pipe **client** — start/stop/status/sample pull |
| File log | Append-only structured records (backup if pipe disconnected) |

**Protocol:** versioned fixed messages (max 4 KB); commands: `PING`, `STATUS`, `ARM`, `BEGIN`, `STOP`, `PULL_SAMPLES`.

**Properties:**

- Local machine only
- No internet
- Explicit start/stop
- Controller disconnect → research continues sampling but transitions to file-only; no block on game thread
- Bounded queue; drop policy documented (never unbounded heap growth)

**Rejected for gen-1:**

| Channel | Reason |
| --- | --- |
| stdout only | Insufficient structure; console absent in GTA |
| Shared memory without protocol | Harder to version/test |
| TCP/socket | Unnecessary attack surface |
| Polling game memory from external after inject | Defeats in-process purpose for coordination |

---

## 14. Vanilla GTA isolation guarantees

| Guarantee | Mechanism |
| --- | --- |
| Normal Steam launch unchanged | No persistent proxy/DLL in Win64 |
| No Rockstar Documents modification | Research output outside game dir |
| No save manipulation | Not in research path |
| No Story Mode suppression | Reject legacy SP guard patterns |
| Preflight gate | Block research if contaminants present |
| M1 dir guard | Block legacy `version.dll` beside M1 exes |
| Explicit launcher | Research runs only when operator invokes launcher |
| Cleanup verification | Launcher post-flight scan Win64 for artifacts (future) |
| One-shot injection | Prefer load from M1 absolute path; no copy-into-game-dir step |

**Incident prevention:** The Task #6 contamination class (M1 tool importing hijacked
`VERSION.dll`) must never recur — research bootstrap **must not** use the name `version.dll`.

---

## 15. Test strategy (before implementation)

| Area | Test type | GTA required? |
| --- | --- | --- |
| Research substate transitions | Unit (foundation/adapter tests) | No |
| Disabled-by-default | Unit | No |
| Research allowlist vs supported registry separation | Unit | No |
| Wrong SHA rejection | Unit | No |
| Malformed experiment request | Unit | No |
| Capability mask rejection | Unit | No |
| Bounded sample limits | Unit | No |
| Pointer validation helpers | Unit | No |
| Evidence record serialize/parse | Unit | No |
| Pipe protocol codec | Unit | No |
| Controller disconnect handling | Unit (mock pipe) | No |
| Clean shutdown ordering | Runtime Harness extension | No |
| Experiment failure isolation | Runtime Harness | No |
| No readiness promotion | Adapter unit tests | No |
| No supported-registry promotion | `supported_builds` tests | No |
| Launcher preflight integration | Unit + fixture | No |
| End-to-end passive read | **Controlled live GTA** | Yes (one PC; architect approval) |

Synthetic fixtures: fake module maps, fake pointers, mock process identity — no ASLR dependency.

---

## 16. First experiment acceptance rules (Task #9B)

A #9B experiment is acceptable **only if**:

- [ ] `PASSIVE_READ` only
- [ ] Bounded (max reads, max samples, max rate)
- [ ] Falsifiable hypothesis stated upfront
- [ ] Derived from **independent evidence** (not closed paths)
- [ ] Exact-build gated via research allowlist
- [ ] Reversible (no persistent game dir changes)
- [ ] No engine calls, hooks, writes, scans
- [ ] Answers **one narrow question**
- [ ] Maximum conclusion pre-declared (usually ≤ `BEHAVIORAL_CORRELATION`)
- [ ] Architect approval before live run

**Not acceptable:** random addresses, GWorld slots, kProfiles retune, semantic string chasing,
PersistentLevel scan, fallback coordinates.

### 16.1 Does repository evidence justify a specific #9B experiment?

**Partially — not for local-player identity.**

Task #8 provides **one independent, validated observation target** suitable for **in-process
plumbing validation only**:

| Target | Evidence source | Narrow #9B question (example) |
| --- | --- | --- |
| gGameState byte @ derivation `MENU_MAINPRESENTS_LEA_CMP_IMM9` | Task #8B live (external) | *"Does in-process PASSIVE_READ reproduce FRONTEND(7) vs POST_FRONTEND(9) correlation using the same static derivation, without RPM?"* |

This experiment:

- Does **not** claim `LOCAL_PLAYER_READY` or `LOCAL_PLAYER_IDENTIFIED`
- Does **not** reopen closed GWorld/profile paths
- Validates research bootstrap + sampler + evidence export only
- Maximum conclusion: **`BEHAVIORAL_CORRELATION`** for gGameState only

**No #9B local-player identity experiment is justified** by current repository evidence.

Local-player work must wait for a **new evidence source** discovered under in-process constraints
(or future architect-directed hypothesis) — not Task #7/#8 closed paths.

---

## 17. Production boundary

| Concern | Rule |
| --- | --- |
| Supported registry | Research allowlist writes ** forbidden** |
| Compatibility | Research must not auto-promote to `Supported` |
| Readiness APIs | Remain `NOT_IMPLEMENTED` until separate authorization |
| `IGameAdapter` | No research experiment handles exposed |
| Qualification | External qualification unchanged; not used as research backdoor |
| Core / foundation | No GTA offsets in shared headers |

---

## 18. Unresolved questions

1. **Injection primitive policy** — Is `CreateRemoteThread` + `LoadLibraryW` acceptable to architect, or required to go through a future signed launcher only?
2. **ABI version bump timing** — Extend `DefluxRuntimeInitContext` vs sidecar config file read by runtime after load?
3. **Anti-cheat / EAC** — SA DE may restrict injection; legal/ToS review outside code scope.
4. **Sampler thread vs game-thread reads** — Confirm gGameState global is safely readable from worker thread (likely yes for plain byte read; needs live validation in #9B plumbing experiment only).
5. **Evidence retention** — Log rotation, max disk per session.
6. **Multi-experiment coexistence** — One active experiment per attach for gen-1 (recommended yes).

---

## 19. Architect decisions required before implementation

| # | Decision |
| --- | --- |
| AD-1 | Approve **Explicit Research Launcher** bootstrap (reject version proxy permanently) |
| AD-2 | Approve gen-1 capability **`PASSIVE_READ` only** |
| AD-3 | Approve **`RESEARCH_BUILD_ALLOWLIST`** separate from supported registry |
| AD-4 | Approve **named pipe + structured file log** control/output |
| AD-5 | Approve **dedicated sampler thread** (no hooks gen-1) |
| AD-6 | Approve Task **#9B plumbing experiment** (gGameState in-process replication) OR defer all live in-process work |
| AD-7 | Reject any #9B experiment touching local-player identity until new evidence class exists |
| AD-8 | Confirm injection mechanism policy for SA DE |

---

## Task #9B Gate 1A — Pre-live hardening

### Authoritative controller ownership

The live experiment controller is owned exclusively by `DeFlux_Runtime.dll` in
`runtime/src/in_process_research_host.cpp` as `g_authoritative_research_controller`.
Runtime Host shutdown calls `deflux::runtime::shutdown_research_from_host()`, which
operates on that single instance.

Foundation tests use **stack-local** `InProcessResearchController` instances. They do
not prove live ownership. Module-boundary proof is in `DeFlux_Runtime_Harness` via
`research_harness.h` exports (`deflux_runtime_research_controller_identity`,
`deflux_runtime_research_harness_async_shutdown_test`, etc.).

There is no `global_in_process_research_controller()` in the game adapter. Linking
`DeFlux_GameAdapter_GtaSaDe.lib` into other executables does not create a competing
live controller unless those executables also define an authoritative host (only Runtime
does).

### M1 research composition (Runtime ↔ game adapter)

**Architect decision (Gate 1B):** M1 Runtime ↔ GTA SA DE adapter **static composition is
accepted only for the bounded #9B research generation**. It is **not** automatically the
final production composition architecture.

For #9B, `DeFlux_Runtime.dll` statically links `DeFlux_GameAdapter_GtaSaDe.lib`. GTA-specific
research types remain in the game adapter partition; generic `abi.h` stays GTA-agnostic.

**Architect decision (Gate 1B):** `research_harness.h` is a **test/research validation
surface only**. It is **not** part of the stable public Runtime ABI.

Future decoupling: move research host behind an explicit composition boundary (e.g.
research-only runtime variant or deferred dynamic adapter load) without changing generic
`deflux_runtime_*` exports.

### Test-only seams

`synchronous_sampling` was removed from `InProcessResearchConfig`. Tests use
`in_process_research_test_seam.hpp` helpers (poll/wait, forced derived address,
internal sample-once). Live configuration cannot enable a synchronous/test execution mode.

---

## Task #9B Gate 2A — Explicit research load mechanism (synthetic only)

Gate 2A implements the bounded CreateRemoteThread + LoadLibraryW research loader.
**No live GTA injection** was performed. Validation uses `DeFlux_ResearchLoader_Fixture.exe`.

### Operator action

- Default: preflight only
- Explicit `--inject` required for load attempt
- Rejects `--pid`, `--dll`, `--target` overrides (not a generic injector)
- Runtime DLL path is trusted M1 layout only (`DeFlux_Runtime.dll` beside launcher)

### Process rights (injection handle)

| Right | Purpose |
| --- | --- |
| `PROCESS_CREATE_THREAD` | `CreateRemoteThread` → remote `LoadLibraryW` |
| `PROCESS_QUERY_INFORMATION` | identity re-check, `IsWow64Process2`, module snapshot |
| `PROCESS_VM_OPERATION` | `VirtualAllocEx` for UTF-16 path buffer |
| `PROCESS_VM_WRITE` | `WriteProcessMemory` for path buffer **only** |

Preflight/TOCTOU identity uses `PROCESS_QUERY_LIMITED_INFORMATION` separately.

### LoadLibraryW resolution

Same-architecture offset strategy: local `kernel32.dll` + `LoadLibraryW` offset applied to
remote `kernel32.dll` base from `CreateToolhelp32Snapshot(TH32CS_SNAPMODULE)`.

### TOCTOU / process identity

Uses Task #4 `ProcessInstance` (PID + creation FILETIME + executable path). Identity is
re-queried immediately before injection; mismatch → `TARGET_IDENTITY_MISMATCH`.

### Bounded wait / cleanup

Default remote thread wait: **30 s**. On all paths: close thread/process handles, `VirtualFreeEx`
path buffer when possible. Timeout reports `REMOTE_THREAD_TIMEOUT` without terminating GTA/target.

### DLL load vs experiment activation

**Separate.** Remote load only maps `DeFlux_Runtime.dll`. `DllMain` remains minimal.
Research controller stays **DISABLED** by default after load. Explicit experiment activation
(for Gate 2B) is **deferred** — no hidden activation in `DllMain`, no named pipe in #9B.

### Gate 2B prerequisites

1. Architect authorization for live GTA `--inject`
2. Manual vanilla GTA launch by operator
3. Future explicit research activation mechanism (post-load)
4. Live gGameState observation protocol sign-off

---

## Task #9B — Implementation (Gate 1 complete; Gate 2 not executed)

**Status:** Plumbing implemented and validated **with GTA closed**. Live injection **not
authorized** in Gate 1.

### Experiment

| Field | Value |
| --- | --- |
| ID | `GTA_SA_DE_GGAMESTATE_OBSERVATION_V1` |
| Version | `1` |
| Capability | `PASSIVE_READ` only |
| Derivation | `MENU_MAINPRESENTS_LEA_CMP_IMM9` (shared with Task #8B) |
| Payload | 1 byte (gGameState) |
| Pointer depth | 0 |
| Max conclusion (live) | `IN_PROCESS_SIGNAL_REPRODUCED` |

**Forbidden conclusions:** `WORLD_READY`, `LOCAL_PLAYER_READY`, `LOCAL_PLAYER_IDENTIFIED`,
`SUPPORTED`, local-player semantics.

### Research allowlist

Separate from production registry in `research_build_allowlist.*`. Only SHA
`63e633bb…846f` is allowlisted. Production `kSupportedSha256{}` remains empty.

### Lifecycle

`InProcessResearchController` substate: `DISABLED` (default) → `ARMED` → `OBSERVING` →
`STOPPING` → `STOPPED` / `FAILED`. Runtime Host `shutdown()` calls
`in_process_research_shutdown_from_runtime_host()`.

### Read / thread bounds

| Bound | Value |
| --- | --- |
| Bytes per sample | 1 |
| Pointer depth | 0 |
| Default interval | 500 ms |
| Interval range | 50–5000 ms |
| Max samples | 120 |
| Consecutive read failures | 10 → `FAILED` |

Sampler: dedicated thread when not in synchronous test mode; no hooks/engine calls.

### Evidence schema

`InProcessResearchEvidenceSample` — experiment id/version, SHA, capability, sample index,
monotonic time, derivation status/RVA, read status/value, substate, failure reason.

### Bootstrap (Gate 1)

`DeFlux_ResearchLauncher.exe` — preflight only. `--inject` is **blocked** with exit code 2.
No `version.dll` proxy. No persistent GTA directory artifacts. Injection deferred to Gate 2.

### Gate 2B Phase 1 — Activation path (implemented; live NOT authorized)

**Status:** Intent mechanism, runtime revalidation, deferred bootstrap, evidence logging, and
closed-game synthetic tests implemented. **Live GTA `--inject` remains blocked** until architect
approves Gate 2B Phase 2.

#### Intent mechanism

Per-session **Local file mapping** (not named pipe, not GTA files):

- Name: `Local\DeFlux_9B_GGameState_ResearchIntent_v1`
- Magic: `DF9B` (live GTA), `DF9S` (synthetic fixture only)
- Schema version: `1`
- Fields: target PID, creation identity, session nonce, experiment id/version, capability,
  interval, max_samples, expected SHA-256, publish timestamp, state

Launcher publishes **Pending** intent immediately before `LoadLibraryW`. Runtime consumes or
rejects after revalidation. Launcher waits (bounded), then closes its mapping handle.

Stale protection: max age 120 s, wrong PID/creation identity, non-Pending state, or consumed
intent all reject activation.

#### Runtime-side revalidation (does not trust launcher)

After deferred host init, `try_activate_research_from_intent()`:

1. Open intent mapping; absent → research stays **DISABLED**
2. Validate magic, schema, bounds, capability == `PASSIVE_READ`
3. Verify experiment == `GTA_SA_DE_GGAMESTATE_OBSERVATION_V1` v1 only
4. Match current PID + creation identity + executable path
5. Live (`DF9B`): host must be `SanAndreas.exe` in GTA SA DE layout; SHA-256 fingerprint must
   match intent and research allowlist; environment preflight must PASS
6. Synthetic (`DF9S`): host must be `DeFlux_ResearchLoader_Fixture.exe` only
7. Configure → arm → mark intent **Consumed** → begin observation

Failure at any step: intent **Rejected**, research **DISABLED** / **FAILED** safely.

#### DllMain boundary and initialization path

```text
Remote LoadLibraryW(DeFlux_Runtime.dll)
  → DllMain(DLL_PROCESS_ATTACH): DisableThreadLibraryCalls + _beginthreadex(deferred bootstrap)
  → deferred CRT thread (outside loader lock):
       deflux_runtime_initialize()
       publish bootstrap_complete
       try_activate_research_from_intent()
```

No SHA hashing, PE scanning, sampling, or thread joins inside `DllMain`.
Deferred bootstrap **must** use `_beginthreadex` (not `CreateThread`) so the CRT is attached
before any `std::mutex` / sampler work.

#### Sampler thread lifecycle invariant

- Sampler workers use `_beginthreadex` (CRT-aware), never a joinable `std::thread` left unjoined.
- Destroying a joinable `std::thread` calls `std::terminate` → `abort()` (exit code 3).
- Controller destructor and `shutdown()` always join/close the sampler handle.
- Live GTA: `begin_observation()` starts an async CRT sampler thread.
- Synthetic fixture (`DF9S`) pipeline proof: `begin_observation_on_calling_thread()` runs the
  bounded sampler on the deferred bootstrap thread (same observation loop, same evidence path).
  Async auto-stop is stress-proven separately in the Runtime Harness (100 iterations).

#### Phase 1 abort incident (root cause)

During Gate 2B Phase 1 Foundation runs, `DeFlux_Foundation_Tests.exe` aborted (`abort()` /
exit code 3) around sampler completion tests. Root causes:

1. **Joinable `std::thread` destruction → `std::terminate`/`abort`:** if the sampler finished
   (or was still running) and the controller was destroyed or reassigned without `join()`,
   the C++ runtime aborted. Fixed by replacing `std::thread` with `_beginthreadex` + explicit
   `WaitForSingleObject`/`CloseHandle`, and joining in the destructor/`shutdown()`.
2. **`CreateThread` deferred bootstrap without CRT attach:** DllMain originally used
   `CreateThread` for bootstrap; CRT-backed activation/sampler then misbehaved under remote
   `LoadLibrary`. Fixed by `_beginthreadex` for deferred bootstrap.
3. **`FreeLibrary` while deferred bootstrap still running** in same-process tests also produced
   fatal teardown. Tests wait for `bootstrap_complete` and do not unload early.

Regression coverage: `test_auto_stop_leaves_no_joinable_sampler_thread`, Runtime Harness
100-iteration auto-stop / shutdown / failure stress exports.

#### Evidence output

Append-only log under `%LOCALAPPDATA%\DeFlux\research\` (outside GTA / Rockstar Documents):

- `SESSION_START` — PID, creation identity, nonce, experiment, expected/observed SHA
- `SAMPLE` — index, monotonic ms, read ok/fail, raw byte, substate, derivation, RVA
- `SESSION_END` — final substate, classification (observation-only), reason

No memory dumps. Runtime does **not** infer MENU/GAMEPLAY/PAUSE from byte values.

#### Bounded observation

Default: interval 500 ms, max 120 samples (~60 s). On max samples: `OBSERVING` → `STOPPING` →
`STOPPED`. DLL may remain loaded; no remote unload in #9B.

Classification: raw evidence only; `IN_PROCESS_SIGNAL_REPRODUCED` is reserved for post-run
architect review with manual phase correlation.

#### Phase 2 live failure boundary (Gate 2B Phase 2A)

Live evidence (`ggamestate_observation_30320_45722801.log`) is preserved outside Git
under `%LOCALAPPDATA%\DeFlux\research\`. A local analysis copy may exist under
`docs/architecture/evidence/phase2-live/` and must not be committed:

- `SESSION_START` … `pipeline_proof=0`
- `SAMPLE index=0` `read_ok=1` `byte=7` (MENU-consistent with Task #8B)
- **no** further samples, **no** `SESSION_END`

No SanAndreas/GTA Application Error or WER APPCRASH was found in the Phase 2 time window.
Absence of `SESSION_END` cannot distinguish crash vs abrupt `TerminateProcess`/clean exit.

**Exact next operations after persisted SAMPLE index=0** (live async sampler):

1. `research_activation_status_publish_controller` (still under controller mutex)
2. release mutex
3. `SAMPLE_WAIT_BEGIN` breadcrumb → `Sleep(interval_ms)` → `SAMPLE_WAIT_END`
4. loop → SAMPLE index=1

Phase 2A adds lifecycle breadcrumbs + outer exception containment on bootstrap/sampler, and a
**synthetic async** remote-load E2E (`DEFLUX_RESEARCH_SYNTHETIC_ASYNC=1`) that exercises the
same `_beginthreadex` path as live GTA. Calling-thread synthetic pipeline proof remains.

Classification of Phase 2: **ROOT_CAUSE_IDENTIFIED**.

**Evidence-supported cause:** after remote `LoadLibrary`, the deferred CRT bootstrap worker
returned (`BOOTSTRAP_THREAD_EXIT`) while the async `_beginthreadex` sampler was still
executing CRT-backed work. Synthetic async repro interleaved `BOOTSTRAP_THREAD_EXIT` with
`SAMPLE_*` and then faulted in `ucrtbased.dll` (`c0000005`, offset `0x181701`). Calling-thread
synthetic never exits bootstrap until sampling finishes and did not reproduce the fault.

**Mitigation in tree:** async activation keeps the bootstrap CRT thread alive until the
controller reaches `STOPPED`/`FAILED` (then joins the sampler) before `BOOTSTRAP_THREAD_EXIT`.

#### Gate 2B live runbook (historical — executed; Task #9B closed)

The authorized second live attempt has been executed and reviewed. Do not repeat it as
part of Task #9B. Task #10 requires a separate architect-designed experiment.

#### Gate 2B live runbook (as executed)

1. Build clean M1 Debug (`build.cmd Debug M1`)
2. Contamination guard PASS
3. Launch vanilla GTA manually; reach main menu
4. Operator marks **MENU** observation period (manual notes)
5. Run `DeFlux_ResearchLauncher.exe --inject` from M1 output dir
6. Enter Story / New Game normally (no automation, teleports, or script suppression)
7. Operator marks **GAMEPLAY_STANDING** period
8. Open pause menu; operator marks **PAUSE_MENU** period
9. Wait for bounded experiment STOP (~60 s max)
10. Exit GTA normally
11. Collect research log from `%LOCALAPPDATA%\DeFlux\research\`
12. Run closed regression (`build.cmd Debug M1`, Foundation + Harness + TestClient)
13. Inspect GTA directory and Rockstar Documents for contamination
14. Architect reviews raw evidence; assigns phase correlation and optional
    `IN_PROCESS_SIGNAL_REPRODUCED`

Task #8B reference values (7 in MENU, 9 elsewhere) are **not** forced pass criteria.

#### Rollback / cleanup

- No GTA directory or Rockstar Documents modification by DeFlux in #9B
- Research log is outside game paths; delete `%LOCALAPPDATA%\DeFlux\research\*.log` if desired
- Intent mapping is session-local; launcher closes handle after inject attempt
- Normal GTA exit tears down injected DLL with process; no remote `FreeLibrary`

---

## Task #9B closure

### Second live attempt (accepted)

One authorized live injection of `GTA_SA_DE_GGAMESTATE_OBSERVATION_V1` (`PASSIVE_READ`)
against SHA-256 `63e633bb51c35fe35ca98f503b62e7ddb8403abe4c9d501cfa9162892453846f`.

Verified facts:

- Live GTA PID `30012`. Exactly one remote `LoadLibraryW` of the trusted M1 `DeFlux_Runtime.dll`.
- Runtime initialized. Intent validated (`detail=live`). Async sampler created and entered.
- At least 14 confirmed MENU samples before navigation was requested: `read_ok=1`, `byte=7`.
  GTA survived the first five of those samples.
- Total samples: **120**. Runtime read failures: **0**. Every recorded byte was **7**.
- Sampler hit the maximum bound and the normal terminal lifecycle was observed:
  `SAMPLER_STOP_BEGIN` → `SAMPLER_STOPPED` → `SESSION_END` → `SAMPLER_THREAD_EXIT` →
  `ASYNC_BOOTSTRAP_WAIT_DONE` → `BOOTSTRAP_THREAD_EXIT`.
- GTA remained alive and responsive after research `STOPPED`, then exited normally by user action.
- No crash, Fatal Error, or freeze/hang was observed.
- GTA directory stayed free of DeFlux files.
- Rockstar Documents had no DeFlux-named modifications. GTA itself updated ordinary files
  (`2CD7C854.dat`, `GameUserSettings.ini`, `cloudsavedata.dat`). Those are not DeFlux contamination.

The first live attempt (PID `30320`) loaded, wrote sample 0 (`byte=7`), then the host
exited before `SESSION_END`. Phase 2A identified the bootstrap/sampler CRT lifetime race
and kept the bootstrap thread alive until the sampler reached `STOPPED` or `FAILED`.
The second attempt is the live proof of that fix: **`LIVE_LIFECYCLE_FIX_VALIDATED`**.

### Signal limit

Only MENU was manually correlated: **MENU → byte 7**. Later samples are not assigned to
LOADING, GAMEPLAY_STANDING, GAMEPLAY_MOVING, or PAUSE_MENU, because reliable manual
timestamps were not recorded.

Byte 7 is not `WORLD_READY`, `ENGINE_READY`, `LOCAL_PLAYER_READY`, gameplay, loading, or a
universal state. Task #8B externally observed MENU = 7 and post-frontend = 9. This
in-process attempt did **not** reproduce a manually correlated post-frontend 9.
Signal classification remains **`IN_PROCESS_SIGNAL_INSUFFICIENT`**.

Readiness stays `NOT_IMPLEMENTED`. The production supported-build registry stays empty.
Compatibility stays `DETECTED_BUT_UNSUPPORTED`.

### PID-reuse caveat

`lifecycle_30012.log` also contains an earlier synthetic bootstrap (`detail=synthetic`)
because the operating system reused PID 30012. The live sequence is the later one marked
`detail=live`. The earlier synthetic sequence is not GTA evidence. Lifecycle files are
not uniquely identified by PID alone. Logging is not redesigned in this close.

### What this close does not establish

- Local-player identity was not researched in Task #9B. It remains not implemented.
- GWorld remains without a qualified candidate. Tasks #7 and #8 are not reopened.
- Universal loader-lock safety is not claimed.

### Next task (not started)

**Task #10A** concluded **`NO_JUSTIFIED_EXPERIMENT`**. See
[local-player-identity-experiment-design.md](local-player-identity-experiment-design.md).
The in-process host is an access capability. Local-player identity remains
**NOT ESTABLISHED**.

**Task #10B — Current-Build Static Local-Ownership Discovery** is the named next
direction and is not started here. It must not begin from GWorld, from M07
(`0x050250E8` / `0x05725750`), from legacy local-player profiles, or from a
coordinate scan, and it must not assume a stock UE4 layout or implement a live
experiment without a later architect review.

The long-term visual sequence remains local-player identity, then transform, remote
entity, simulated Player 2, name tag, and health bar. None of those later stages are
authorized by this finalization.

---

## Related documents

- [Local-player identity experiment design](local-player-identity-experiment-design.md) — Task #10A: no justified experiment
- [Runtime readiness research](runtime-readiness-research.md) — Task #8 evidence checkpoint
- [Runtime host](runtime-host.md) — M1 host ABI and lifecycle
- [Runtime qualification](runtime-qualification.md) — external qualification vs readiness
- [Vanilla validation](../development/vanilla-validation.md) — contamination rules
