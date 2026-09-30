# Build

How to compile the Visual Studio tree. This is not a deploy guide for a production multiplayer platform.

This snapshot contains:

- **Launcher and server sources** — `DeFlux_Server`, `DeFlux_Launcher`, `version_proxy`. The legacy injected client (`DeFluxCore`) is not included.
- **M1 / research sources** — `DeFlux_Foundation`, `DeFlux_GameAdapter_GtaSaDe`, `DeFlux_TestClient`, `DeFlux_Foundation_Tests`, `DeFlux_Runtime`, `DeFlux_Runtime_Harness`, plus the research launcher and loader fixture invoked by `build.cmd Debug M1`.

M1 targets compile and run without launching GTA. Early adapter stages are passive detection and fingerprinting. Later research code can read process memory and contains a research DLL loader. Offline tests do not prove live integration. The Runtime Host harness loads `DeFlux_Runtime.dll` into the harness process only. Do not treat a green test run as a supported game build.

## Prerequisites

- Windows 10/11 x64
- Visual Studio 2022 (or later) with MSBuild and the C++ x64 toolset
- Windows 10 SDK (`WindowsTargetPlatformVersion` is `10.0` in the projects)
- C++20 (`LanguageStandard` is `stdcpp20`)

Optional, launcher NUI only:

- Node.js 18 or later
- From `launcher/`: `npm start` runs `host/index.js` (local static UI + WebSocket bridge). Native `DeFlux_Launcher.exe` does not require Node to build.

The solution file is Visual Studio 17 format. `DeFlux.Toolset.props` selects platform toolset `v143` when that toolset is installed; otherwise it uses the VS default. Local object files may therefore show a newer compiler (for example `vc145`) without any project file change.

## Solution and projects

Root solution: `DeFlux.sln` (platform **x64** only; configurations **Debug** and **Release**).

**LEGACY PROTOTYPE**

| Project | Path | Output |
| --- | --- | --- |
| `DeFlux_Server` | `server/DeFlux_Server.vcxproj` | `build/bin/<Config>/legacy/DeFlux_Server.exe` |
| `DeFlux_Launcher` | `launcher/DeFlux_Launcher.vcxproj` | `build/bin/<Config>/legacy/DeFlux_Launcher.exe`. This snapshot does not copy `DeFlux_clean.sav`. Slot 8 deployment is unavailable without that excluded template. |
| `version_proxy` | `launcher/version_proxy/version_proxy.vcxproj` | `build/bin/<Config>/legacy/version.dll` |

**NEW M1 FOUNDATION**

| Project | Path | Output |
| --- | --- | --- |
| `DeFlux_Foundation` | `foundation/DeFlux_Foundation.vcxproj` | `build/bin/<Config>/m1/DeFlux_Foundation.lib` |
| `DeFlux_GameAdapter_GtaSaDe` | `game_adapters/gta_sa_de/DeFlux_GameAdapter_GtaSaDe.vcxproj` | `build/bin/<Config>/m1/DeFlux_GameAdapter_GtaSaDe.lib` |
| `DeFlux_TestClient` | `test_client/DeFlux_TestClient.vcxproj` | `build/bin/<Config>/m1/DeFlux_TestClient.exe` |
| `DeFlux_Foundation_Tests` | `tests/foundation/DeFlux_Foundation_Tests.vcxproj` | `build/bin/<Config>/m1/DeFlux_Foundation_Tests.exe` |
| `DeFlux_Runtime` | `runtime/DeFlux_Runtime.vcxproj` | `build/bin/<Config>/m1/DeFlux_Runtime.dll` |
| `DeFlux_Runtime_Harness` | `tests/runtime/DeFlux_Runtime_Harness.vcxproj` | `build/bin/<Config>/m1/DeFlux_Runtime_Harness.exe` |

M1 dependency direction: foundation ← GTA SA DE adapter; foundation ← test client / foundation tests; foundation ← runtime. The runtime harness project-references the DLL for build order but sets `LinkLibraryDependencies=false` and resolves exports with `GetProcAddress`. Foundation does not reference the adapter or the runtime.

Shared headers: `shared/include/` (`protocol.hpp`, `net_types.hpp`). Server and test code include them directly. There is no separate shared library.

The legacy `DeFluxCore` client referenced by historical research notes is intentionally excluded from this archival snapshot. `vendor/minhook/` is excluded with it. This snapshot does not compile that stub.

MSBuild layout is centralized:

- `Directory.Build.props` — `DeFluxRoot`
- `DeFlux.Toolset.props` — platform toolset
- `DeFlux.Output.M1.props` — M1 `OutDir` / `IntDir` under `build\bin\<Config>\m1\`
- `DeFlux.Output.Legacy.props` — legacy `OutDir` / `IntDir` under `build\bin\<Config>\legacy\`

**Hard output boundary:** M1 executables and the legacy `version.dll` proxy must never share one runtime directory. M1 tools import Windows `VERSION.dll` for PE file-version APIs; a colocated `version.dll` proxy was written to load the omitted `DeFluxCore` client.

Obsolete: the old shared `build\bin\<Configuration>\` root (without `m1\` or `legacy\`) must not be used for execution.

## Command line

From the repository root:

```bat
build.cmd
build.cmd Debug
build.cmd Release
build.cmd Debug M1
build.cmd Debug clean
```

**Default = M1 build.** These commands build the M1 targets, including the research launcher and loader fixture. They do not build the server, launcher, or version proxy.

Explicit legacy / full solution (historical research; outputs under `build\bin\<Configuration>\legacy\`):

```bat
build.cmd Debug legacy
build.cmd Release full
```

Manual copy of legacy outputs from `build\bin\<Configuration>\legacy\` into `Gameface\Binaries\Win64` **contaminates the vanilla validation environment**. See [Vanilla validation](vanilla-validation.md).

`build.cmd` locates MSBuild with `vswhere`. M1 binaries: `build\bin\<Configuration>\m1\`. Legacy binaries: `build\bin\<Configuration>\legacy\`.

Open `DeFlux.sln` in Visual Studio if you prefer the IDE. Use the M1 project subset for everyday work; building the entire solution in VS has the same legacy contamination risk as `build.cmd … legacy`.

## Outputs (generated; not source)

Typical Release layout after a successful build:

```text
build/bin/Release/m1/
    DeFlux_Foundation.lib
    DeFlux_GameAdapter_GtaSaDe.lib
    DeFlux_TestClient.exe
    DeFlux_Foundation_Tests.exe
    DeFlux_Runtime.dll
    DeFlux_Runtime_Harness.exe

build/bin/Release/legacy/
    DeFlux_Server.exe
    DeFlux_Launcher.exe
    version.dll
```

`DeFluxCore` and `DeFlux_clean.sav` are not outputs of this snapshot. The legacy DeFluxCore client referenced by historical research notes is intentionally excluded from this archival snapshot.

Run M1 checks without GTA (always from the `m1` directory):

```bat
build\bin\Debug\m1\DeFlux_Foundation_Tests.exe
build\bin\Debug\m1\DeFlux_TestClient.exe
build\bin\Debug\m1\DeFlux_Runtime_Harness.exe
```

`build/`, `.vs/`, `*.user`, PDBs, and object files are local/generated. They are listed in `.gitignore`. This snapshot does not contain a `client/` tree.

## What this build does not do

- The default M1 build does not build the server, launcher, or version proxy.
- It does not install DeFlux into a game directory.
- It does not start Steam or GTA.
- It does not prove multiplayer correctness.
- M1 detection and qualification do not modify a GTA install.
- This snapshot does not include the MinHook stub or the upstream MinHook library.

## Related

- [Architecture overview](../architecture/overview.md)
- Root [README](../../README.md)
