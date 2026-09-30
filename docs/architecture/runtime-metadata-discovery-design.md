# Task #10C — Runtime metadata discovery design

**Status:** Design and static audit only. **Not implemented.** GTA remained closed.
No injection. No live memory read. No commit.

**Checkpoint:** `3e004d49a3e281eefe174a7496f88cc1ee38f06e`

**Conclusion:** `NO_METADATA_EXPERIMENT_JUSTIFIED`

This task did not search for the local player. Task #10A remains
`NO_JUSTIFIED_EXPERIMENT`. Task #10B remains `NO_LIVE_EXPERIMENT_JUSTIFIED`.

## Build

| Field | Value |
| --- | --- |
| SHA-256 | `63e633bb51c35fe35ca98f503b62e7ddb8403abe4c9d501cfa9162892453846f` |
| Image base | `0x140000000` |
| SizeOfImage | `0x05CA4400` |
| SHA verified offline | Yes |

Sections read: `.text`, `.rdata`, `.data`, `.pdata`. The `.data` raw bytes end
at RVA `0x0512D600`. Virtual `.data` continues past that. Bytes past the raw
end are not in the file and were not read from a process.

## Question

Does this build expose enough current-build evidence to justify one future
passive experiment about runtime object or type identity?

Not: where is the local player?

## Method and bounds

Order was semantics, then code, then data. No candidate global was used as a
start. Closed routes from Tasks #7, #8, #10A, and #10B were not reopened.
Stock UE layouts were not imported.

Instruction decoding reused the unambiguous RIP-relative `LEA`/`MOV` modrm
`0x05` pattern. Uncertain decodes were not promoted.

| Bound | Limit | Result |
| --- | --- | --- |
| RTTI descriptors retained for engine/game use | 64 | 0 retained. 604 `.?AV` names were counted, then rejected as non-engine |
| Metadata/name anchors | 64 | 7 token hits inspected |
| Metadata-consuming code regions | 32 | Exceeded by references to `/Script/Engine` and `/Script/CoreUObject` pointer slots. Analysis stopped. One call site was verified |
| High-interest functions | 24 | 1 callee inspected |
| Caller/callee depth | 2 | Stopped at that callee |
| Data relationships | 16 | The companion address is outside the file image |
| Candidate structures | 8 | 0 |
| Recursive pointer walks | none | none |

The code-region cap is why this task does not design an experiment. Expanding
the set would be a new study.

## Three identities

| Identity | What this build supports |
| --- | --- |
| Native C++ type | RTTI for third-party and runtime-library types only |
| Reflected engine type | Not established. Package-path strings are not a class layout |
| Local ownership | Not studied. Still **NONE** |

A vtable or type descriptor does not prove a reflected engine class. A
reflected class name would not prove local ownership. Local-player research
stays closed.

## RTTI

604 decorated `.?AV` names and 41 `.?AU` names. Mangler symbols `??_R0`
through `??_R4` and `??_7` are absent.

| Family | Count |
| --- | --- |
| ICU | 319 |
| STL / templates | 100 |
| Other third-party (PhysX, OpenEXR, OpenSubdiv, GeForce Now, concurrency, loggers) | the rest |
| Player, Pawn, Actor, Viewport, Controller, Camera, UClass, AActor, LocalPlayer | **0** |

The only `UObject` decorated name is `.?AVUObject@icu_64@@`. The only
`Character` names are ICU iterators.

One Complete Object Locator was checked for `.?AVtype_info@@`: signature 1,
offset 0, type-descriptor RVA `0x0512C6B0`. That shows native RTTI machinery
for `type_info`. It does not recover a vftable for an engine or game class,
because those descriptors are not in the image.

**Useful engine/game RTTI descriptors: 0.**
**Vftable relationships for engine/game objects: not recovered.**

## Name tables from Task #10B

`IsLocalPlayerController`, `bIsLocalPlayerController`, `LocalPlayers`,
`PlayerInput`, and `PlayerCameraManager` remain semantic names. This audit
found no code bridge from those name-pointer slots to a runtime object or
class. Name adjacency is not promoted.

## Package-path references

These C strings exist:

| String | RVA |
| --- | --- |
| `/Script/CoreUObject` | `0x044B1410` |
| `/Script/Engine` | `0x04A376D8` |
| `/Script/EngineSettings` | `0x045C13F0` |
| `/Script/EngineMessages` | `0x04885CE8` |

No direct code reference targets the string bytes. Code does reference nearby
pointer slots that hold the preferred-base address of those strings. One site
was checked:

| Item | Value |
| --- | --- |
| Instruction | `0x01CAEBCC` `LEA` |
| Slot | `0x044B62F0` → `/Script/CoreUObject` |
| Companion `LEA` | `0x0576EC78`, past the raw `.data` end, not in the file |
| Callee | `0x01CA13C0`–`0x01CA13F6` (54 bytes, no string references) |

A second site, `0x0373B80C`, is the same `LEA` form aimed at the `/Script/Engine`
slot. Further sites were not analyzed. The set is larger than 32 regions.

`FName` at `0x04444AA3` is the enumerator `EMeshElementAttributeType::FName`.
`IsChildOf` at `0x045C16EC` is `ESubLevelStripMode::IsChildOf`.
`NativeFunction` at `0x04896068` is an indirection-type enumerator. None of
those is a name pool, a cast, or a function.

Absent as tokens: `GUObjectArray`, `FNamePool`, `GNames`, `FUObjectArray`,
`ObjObjects`, `StaticFindObject`, `StaticAllocateObject`,
`StaticConstructObject`, `GetPrivateStaticClass`, `Z_Construct_UClass`,
`UClass`, `BeginDestroy`, `FinishDestroy`, `AddToRoot`, `MarkPendingKill`.

## Retained mechanisms

| ID | What | Classification |
| --- | --- | --- |
| M-RTTI | Native RTTI for non-engine libraries, including one `type_info` locator | **METADATA_CONTRADICTED** as engine/game type identity |
| M-PKG | Code loads a `/Script/...` pointer and an address that is not in the file, then calls a stringless thunk | **METADATA_WEAK** |
| M-ENUM | `FName`, `IsChildOf`, `NativeFunction` as enumerator text | **NOT_METADATA_EVIDENCE** |
| M-NAMES | Task #10B property/method name tables | **NOT_METADATA_EVIDENCE** |

**METADATA_STRONG: 0.**
**METADATA_PROMISING_UNVERIFIED: 0.**
**METADATA_WEAK: 1.**
**METADATA_CONTRADICTED: 1.**
**NOT_METADATA_EVIDENCE: 2.**

No object-registry hypothesis was retained. There is no current-build evidence
of an object count, index, registration, removal, serial check, or iterator.

No name-pool hypothesis was retained. There is no identifier-to-text path and
no invalid-index rejection.

No UObject-like instance header was recovered. No field offset is claimed.

## Live gate

`NO_METADATA_EXPERIMENT_JUSTIFIED`

Missing evidence: a structure, present in this file or described by code that
was actually read, which a passive observation could accept or reject as a
type or name registry. M-PKG does not provide that structure. The companion
address is not in the executable, the callee has no semantic strings, and the
reference set is larger than the allowed analysis bound.

No experiment ID is proposed. No address is promoted. `0x0576EC78` is not a
metadata root. No local-player, controller, pawn, or transform lookup is
authorized.

Task #10B name tables stay semantic names. There is no bridge from those names
to runtime metadata or to an object instance. `IsLocalPlayerController`,
`bIsLocalPlayerController`, `LocalPlayers`, `PlayerController`, and
`PlayerCameraManager` are not runtime identity evidence.

## Static-research stop

Repeated discovery of the form string, then xref, then nearby global, then a
UE-like structural guess has reached its justified stop for this evidence set.
No Task #10D continuation of that strategy is authorized. GWorld discovery,
legacy local-player profiles, M07, the Task #10B weak locality routes,
arbitrary global ranking, coordinate scanning, stock UE4 layouts, and
internet-derived offsets stay closed. A later direction needs a materially
different evidence source.

**Next research direction: REQUIRES ARCHITECT DECISION.**

## Checkpoint regression anomaly

The first Foundation run during this audit was 752 passed, 1 failed.
The failure was `second.duplicate_load_attempt`, the known synthetic
duplicate-load race. The immediate rerun was 753 passed, 0 failed. That
history is part of the record. It does not change `NO_METADATA_EXPERIMENT_JUSTIFIED`.

The fresh checkpoint run that blocked the Task #10C commit was 742 passed,
11 failed. It was not rerun before the fix. The first failure was again
`qualification_research_remote_loader_tests.cpp:218`
`second.duplicate_load_attempt`. The synthetic activation diagnostic was
`load=14 intent=1 bootstrap=1 substate=2 samples=7 obs=0 pipe=0 log=`
with `max_samples=5`.

## Task #10C.1 activation stabilization

Architect review of Task #10C.1 is PASS. Engineering classification:
`REGRESSION_STABILIZED`. The research classification stays
`NO_METADATA_EXPERIMENT_JUSTIFIED`.

Three demonstrated causes are kept. They are not one bug.

**Cause 1 — shared status mapping.** The activation status object was the
global name `Local\DeFlux_9B_ResearchActivationStatus_v1`. A waiter could
read a record written by another process or session.
`publish_bootstrap_complete` set only `bootstrap_complete` and left the
previous sample count and substate in place. The waiter did not match
`session_nonce`. The foreign record could show `substate=OBSERVING` and
`samples_collected=7` while this controller was configured with
`max_samples=5`. That did not demonstrate that the capped controller
collected 7 samples. A controller whose cap is 5 stops when the sample
count reaches 5. `pipeline_proof=0` on that record means the writer had
not bound this synthetic session. The empty `log=` was the timeout return
in `execute_synthetic_activation_e2e`, which left before resolving the
evidence path. The fix is a nonce-scoped mapping,
`Local\DeFlux_9B_ResearchActivationStatus_v1_<nonce>`, plus session and
cap checks on the waiter.

**Cause 2 — Toolhelp interpreted as absence.** `second.duplicate_load_attempt`
failed because a failed `CreateToolhelp32Snapshot` or `Module32First` was
treated as “the runtime module is not loaded.” This is independent of
Cause 1. During a later stability streak the same Toolhelp failure class
appeared at `resolve_remote_loadlibrary_w`: duplicate stress attempt 71
reported `first=14` (`LoadSucceeded`), `second=10`
(`LoadLibraryResolutionFailed`), and `duplicate_load_attempt=1`. That
resolver took one snapshot with `TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32`
and failed the load when the snapshot failed. Snapshot and
`Module32First` failures are now retried. A completed enumeration that
does not contain the module still returns immediately.

**Cause 3 — completion published before exit evidence.** A later async
stress streak, run 8 attempt 82, reported a successful session while
`lifecycle_<pid>.log` did not yet contain `SAMPLER_THREAD_EXIT`. The
sampler published `observation_complete` inside the sample loop, so the
waiter could observe `STOPPED` before the trampoline persisted
`SAMPLER_THREAD_EXIT`. The correction publishes `observation_complete`
only after the sampler has been joined, and writes
`ASYNC_BOOTSTRAP_WAIT_DONE` before that publish.

Stability history, including the stopped streaks:

| Streak | Result | Cause |
| --- | --- | --- |
| Pre-fix checkpoint | 742 passed, 11 failed | Causes 1 and 2. Not rerun. |
| Streak A | Runs 1–2 passed. Run 3 failed. | Cause 2 at duplicate stress attempt 71. Stopped. |
| Streak B | Runs 1–7 passed. Run 8 failed. | Cause 3 at async stress attempt 82. Stopped. |
| Final binary | 20/20, each 770 passed, 0 failed | Accepted. |

Runtime Harness on the accepted binary: 120 passed, 0 failed.

Final contracts:

- One authoritative research controller per runtime process.
- At most one sampler worker per controller session.
- Sample indices are `0 .. max_samples-1`.
- A completed session has `samples_collected == max_samples`.
- Hard invariant: `samples_collected <= configured_max_samples`.
- A successful evidence session contains `SESSION_START`, `SAMPLE index=0` through `SAMPLE index=max_samples-1`, and `SESSION_END`.
- Status mapping is nonce-scoped per session.
- Bootstrap lifetime encompasses sampler lifetime: the bootstrap thread stays alive until the sampler reaches `STOPPED` or `FAILED`, the sampler is joined, and only then the bootstrap exits.

**Next phase: `CONTROLLED_LIVE_BEHAVIORAL_RESEARCH`.**

That phase is not designed here and is not authorized by this document.
It will use the stabilized research activation path. It must not treat
arbitrary changing floats as player coordinates. Static string, xref, and
nearby-global discovery stays closed until there is materially new evidence.

## Trust boundary

| Boundary | Result |
| --- | --- |
| Task #10A | **`NO_JUSTIFIED_EXPERIMENT`** |
| Task #10B | **`NO_LIVE_EXPERIMENT_JUSTIFIED`** |
| Task #10C | **`NO_METADATA_EXPERIMENT_JUSTIFIED`** |
| Useful engine/game RTTI descriptors | **0** |
| Useful engine/game vftable relationships | **0** |
| Reflected engine type identity | **NOT ESTABLISHED** |
| Object-registry hypotheses | **0** |
| Name-infrastructure hypotheses | **0** |
| GUObjectArray-like evidence | **ABSENT** |
| FName / name-pool evidence | **ABSENT** |
| Useful UObject metadata | **ABSENT** |
| Runtime instance identity | **NOT ESTABLISHED** |
| Bounded falsifiable passive metadata observation | **NONE** |
| Local-player identity | **NOT ESTABLISHED** |
| Local-player transform | **NOT ESTABLISHED** |
| Uniquely-local root | **NONE** |
| GWorld | **NO QUALIFIED CANDIDATE** |
| Task #9B activation pipeline | **VALIDATED** |
| Production registry | **EMPTY** |
| Compatibility | **DETECTED_BUT_UNSUPPORTED** |
| Readiness | **NOT_IMPLEMENTED** |

Native C++ RTTI and reflected engine type identity stay separate. Generic RTTI
presence is not Unreal reflected metadata. No RVA from this audit enters
production runtime logic. Every RVA above is **STATIC_RESEARCH_ONLY**.

## Related documents

- [Local-player identity experiment design](local-player-identity-experiment-design.md) — Task #10A, unchanged
- [Local-ownership static discovery](local-ownership-static-discovery.md) — Task #10B, unchanged
