# Negative results

Negative results in this document mean:

**DE:Flux did not establish the target using the documented method and build.**

They do not mean:

**The target cannot exist.**

No raw game dumps or disassembly are included.

## Build these results refer to

| Field | Value |
| --- | --- |
| Executable | `SanAndreas.exe` (Definitive Edition) |
| Version string recorded with the research | `1.0.113.21181` |
| SHA-256 | `63e633bb51c35fe35ca98f503b62e7ddb8403abe4c9d501cfa9162892453846f` |
| Compatibility | `DETECTED_BUT_UNSUPPORTED` |
| Supported-build registry | empty |
| Readiness | `NOT_IMPLEMENTED` |

## GWorld

**Result: NO QUALIFIED CANDIDATE.**

Documented in `docs/architecture/gworld-discovery-research.md` and `docs/architecture/runtime-readiness-research.md`.

| Method | Result | Rules out | Does not rule out |
| --- | --- | --- | --- |
| ASCII search for `GWorld`, `PersistentLevel`, and `OwningGameInstance`, then nearby pointer cells | Classified `LOCAL_PLAYER_EVIDENCE_INVALID`. String presence is not a pointer route. | Treating those strings as a GWorld or local-player locator on this build. | Some other xref or runtime method. |
| `OwningGameInstance` | String present. Offline study recorded 0 code xrefs. | Using that string alone as a game-instance route. | A route that does not use this string. |
| `PersistentLevel` | A string and at least one mixed xref were seen. The xref shared a function with unrelated names such as `RenderThread`. | Treating that xref as identity. | A different field path. |
| Legacy signature patterns `p1` and `p5` (RIP-relative `mov` / `test` shapes in `gworld_discovery.cpp`) | The scanner returns ambiguous when more than one slot RVA matches, and refuses to pick one. | A unique slot from those patterns on this image. | A narrower, reviewed pattern. |
| Structural offline scan of RIP-relative loads | 195,283 raw references, 72,030 after context filters, 86 slots with at least 12 references, shortlist returned 0. | Static convergence as a GWorld proof. | A later live method with independent identity evidence. |
| Offline identity pass | 1 `IDENTITY_SUPPORTED` (`0x05764130`), 38 ambiguous. The supported label was weak and mixed. | Promoting `0x05764130`. | The slot meaning something else. |
| Live reads of `0x05391B70` | Non-null in menu/loading, null in observed gameplay. Trust remains `LEGACY_CANDIDATE_UNVERIFIED`. | Using that historical slot as current GWorld. | A different slot. |
| Address `0x0FE1D1D8` | Outside `SizeOfImage` for this build. Zero references. | That legacy address on this executable. | Addresses inside the image. |

A figure of about 20,331 matches for one pattern is **not** in the exported sources or docs. It is not repeated here as a measurement. The counts above are the ones the research notes record. An offline test print of legacy signature matches (about 18,380 matches, about 17,133 unique slots on one run) is a scanner statistic, not a qualified slot.

## Local player

**Identity: NOT ESTABLISHED.**  
**Transform: NOT ESTABLISHED.**

- Early shared profile slots were either contradicted (`0x05764130` for local-player semantics) or left unverified (`0x05391B70`).
- PersistentLevel actor scans were classified as not the local player.
- Position writes and teleports in the omitted legacy client are classified `MODIFICATION_ONLY` in `docs/architecture/runtime-readiness-research.md`. They are not a read of live player state.
- The private audit of that teleport path was partial. The exported tree does not contain the label `LEGACY_TELEPORT_PROVENANCE_PARTIAL` as a symbol. Do not present the legacy telemetry as a complete live player-state implementation.
- Behavioral transform research did not establish a candidate. The private research record used `NO_TRANSFORM_CANDIDATE_ESTABLISHED` for that study. That token is not a symbol in the exported sources.
- CLEO-grounded research in this snapshot is research, not production support. The live claim function withholds `NO_LIVE_CLAIM`. A first process with a strong coordinate match is labeled `FIRST_PROCESS_CANDIDATE_ONLY`, which is not local-player identity. Live sessions did not establish a valid local-player transform. No playable multiplayer resulted.

## Metadata study

Task #10C, in `docs/architecture/runtime-metadata-discovery-design.md`:

**`NO_METADATA_EXPERIMENT_JUSTIFIED`.**

That closes a metadata experiment on the studied build. It does not say metadata can never describe a player.

## What stayed true after later research

Later research added process-memory reads, signature scans, and a research DLL loader (`VirtualAllocEx`, `WriteProcessMemory` of a DLL path, `CreateRemoteThread` of `LoadLibraryW`).

Those mechanisms did not:

- add any SHA-256 to the supported-build registry
- change readiness from `NOT_IMPLEMENTED`
- establish GWorld
- establish local-player identity or transform
- implement playable multiplayer
