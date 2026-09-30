# DeFlux documentation

This directory records the intended platform architecture and how to build the tree.

It does **not** describe a finished multiplayer product. Distinguish:

- **LEGACY SOURCES IN THIS SNAPSHOT** — `server`, `launcher`, `shared`. The injected client and the MinHook stub are not included.
- **M1 AND RESEARCH** — `foundation`, `game_adapters/gta_sa_de`, `runtime`, `test_client`, `tests/foundation`, `tests/runtime`, `research_launcher`, `tests/research_loader_fixture`

Early passive M1 stages detect an install, fingerprint the executable, and observe process lifecycle without injection. Later isolated research, still in `game_adapters/gta_sa_de/`, added read-only qualification reads and a research loader that can `VirtualAllocEx`, `WriteProcessMemory` a DLL path, and `CreateRemoteThread` `LoadLibraryW`. That research did not mark any build supported, did not implement readiness, and did not prove multiplayer. The legacy injected client (`client/`) is not in this snapshot. Its conclusions are summarized in [NEGATIVE_RESULTS.md](NEGATIVE_RESULTS.md). The Runtime Host exposes a C ABI; loading it into GTA was a research path, not a product.

## Architecture

- [Architecture overview](architecture/overview.md) — logical components, isolation boundaries, and how they map onto the current folders
- [Architectural principles](architecture/principles.md) — approved constraints for future work
- [Runtime Host and bootstrap ABI](architecture/runtime-host.md) — in-process runtime lifecycle and C ABI (not a GTA loader)
- [Runtime qualification](architecture/runtime-qualification.md) — external process probe. Task #6 reads memory. A later research loader is separate and did not mark the build supported.

## Development

- [Build](development/build.md) — Visual Studio solution, configurations, and output layout
- [Vanilla validation](development/vanilla-validation.md) — M1-only build, preflight, contamination, Task #6 status
- [Config reset](development/config-reset.md) — Documents config backup/reset procedure

## Status

The **legacy injected client is not in this snapshot.** Historical notes that name `client/` describe material that was intentionally left out. Server, launcher, and shared sources that remain here were not migrated into a finished multiplayer product.
