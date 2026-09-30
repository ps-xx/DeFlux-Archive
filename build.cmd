@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem DeFlux build script (Visual Studio 2022 / MSBuild)
rem
rem Safe default (M1 only — no legacy runtime rebuild):
rem   build.cmd
rem   build.cmd Debug
rem   build.cmd Release
rem   build.cmd Debug M1
rem
rem Explicit legacy / full solution (legacy outputs under build\bin\<Config>\legacy\):
rem   build.cmd Debug legacy
rem   build.cmd Release full
rem
rem Clean:
rem   build.cmd Debug clean
rem   build.cmd Release clean M1
rem   build.cmd clean

set "CONFIGURATION="
set "BUILD_SET=M1"
set "DO_CLEAN=0"

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="clean" (
    set "DO_CLEAN=1"
    shift
    goto parse_args
)
if /I "%~1"=="Debug" (
    set "CONFIGURATION=Debug"
    shift
    goto parse_args
)
if /I "%~1"=="Release" (
    set "CONFIGURATION=Release"
    shift
    goto parse_args
)
if /I "%~1"=="M1" (
    set "BUILD_SET=M1"
    shift
    goto parse_args
)
if /I "%~1"=="legacy" (
    set "BUILD_SET=LEGACY"
    shift
    goto parse_args
)
if /I "%~1"=="full" (
    set "BUILD_SET=LEGACY"
    shift
    goto parse_args
)
echo [DeFlux] ERROR: Unknown argument: "%~1"
echo [DeFlux] Usage: build.cmd [Debug^|Release] [M1^|legacy^|full] [clean]
exit /b 1

:args_done
if "%CONFIGURATION%"=="" set "CONFIGURATION=Debug"
if "%DO_CLEAN%"=="1" if "%~1" NEQ "" (
    echo [DeFlux] ERROR: Unexpected trailing argument: "%~1"
    exit /b 1
)

set "ROOT=%~dp0"
set "ROOT_NOSLASH=%ROOT:~0,-1%"
set "SOLUTION=%ROOT%DeFlux.sln"
if /I "%BUILD_SET%"=="M1" (
    set "OUT_DIR=%ROOT%build\bin\%CONFIGURATION%\m1"
) else (
    set "OUT_DIR=%ROOT%build\bin\%CONFIGURATION%\legacy"
)

if not exist "%SOLUTION%" (
    echo [DeFlux] ERROR: Solution not found: "%SOLUTION%"
    exit /b 1
)

rem Locate vswhere
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [DeFlux] ERROR: vswhere.exe not found. Install Visual Studio 2022.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set "MSBUILD=%%i"
)

if not defined MSBUILD (
    echo [DeFlux] ERROR: MSBuild.exe not found via vswhere.
    exit /b 1
)

if /I "%BUILD_SET%"=="M1" (
    set "M1_PROJECTS=foundation\DeFlux_Foundation.vcxproj game_adapters\gta_sa_de\DeFlux_GameAdapter_GtaSaDe.vcxproj tests\foundation\DeFlux_Foundation_Tests.vcxproj runtime\DeFlux_Runtime.vcxproj tests\runtime\DeFlux_Runtime_Harness.vcxproj test_client\DeFlux_TestClient.vcxproj research_launcher\DeFlux_ResearchLauncher.vcxproj tests\research_loader_fixture\DeFlux_ResearchLoader_Fixture.vcxproj"
) else (
    set "M1_PROJECTS="
)
set "MSBUILD_CFG=/p:Configuration=%CONFIGURATION% /p:Platform=x64"

echo [DeFlux] MSBuild : "%MSBUILD%"
echo [DeFlux] Solution: "%SOLUTION%"
echo [DeFlux] Config  : %CONFIGURATION%
echo [DeFlux] Set     : %BUILD_SET%
echo [DeFlux] Output  : "%OUT_DIR%"

if "%DO_CLEAN%"=="1" (
    echo [DeFlux] Cleaning...
    if /I "%BUILD_SET%"=="M1" (
        for %%P in (%M1_PROJECTS%) do (
            "%MSBUILD%" "%ROOT%%%P" /t:Clean %MSBUILD_CFG% "/p:SolutionDir=%ROOT_NOSLASH%\\" "/p:DeFluxRoot=%ROOT_NOSLASH%\\" /nologo /v:minimal
            if errorlevel 1 (
                echo [DeFlux] Clean failed for %%P
                exit /b 1
            )
        )
    ) else (
        "%MSBUILD%" "%SOLUTION%" /t:Clean %MSBUILD_CFG% "/p:SolutionDir=%ROOT_NOSLASH%\\" "/p:DeFluxRoot=%ROOT_NOSLASH%\\" /m /nologo /v:minimal
        if errorlevel 1 (
            echo [DeFlux] Clean failed.
            exit /b 1
        )
    )
)

echo [DeFlux] Building...
if /I "%BUILD_SET%"=="M1" (
    echo [DeFlux] M1 targets: DeFlux_Foundation, DeFlux_GameAdapter_GtaSaDe, DeFlux_Foundation_Tests, DeFlux_Runtime, DeFlux_Runtime_Harness, DeFlux_TestClient, DeFlux_ResearchLauncher, DeFlux_ResearchLoader_Fixture
    for %%P in (%M1_PROJECTS%) do (
        "%MSBUILD%" "%ROOT%%%P" /t:Build %MSBUILD_CFG% "/p:SolutionDir=%ROOT_NOSLASH%\\" "/p:DeFluxRoot=%ROOT_NOSLASH%\\" /nologo /v:minimal
        if errorlevel 1 (
            echo [DeFlux] Build failed for %%P
            exit /b 1
        )
    )
) else (
    echo [DeFlux] WARNING: snapshot build — DeFlux_Server, version_proxy, DeFlux_Launcher. DeFluxCore is not in this snapshot.
    echo [DeFlux] WARNING: legacy outputs go to build\bin\%CONFIGURATION%\legacy — never beside M1 executables.
    "%MSBUILD%" "%SOLUTION%" /t:Build %MSBUILD_CFG% "/p:SolutionDir=%ROOT_NOSLASH%\\" "/p:DeFluxRoot=%ROOT_NOSLASH%\\" /m /nologo /v:minimal
    if errorlevel 1 (
        echo [DeFlux] Build failed.
        exit /b 1
    )
)

if /I "%BUILD_SET%"=="M1" (
    echo [DeFlux] Verifying M1 runtime directory safety...
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$dir = '%OUT_DIR%';" ^
        "$blocked = @('version.dll','DeFluxCore.asi','DeFluxCore.dll','DeFluxCore.log','deflux_entry_mode.txt','dinput8.dll','winmm.dll','dxgi.dll');" ^
        "$found = @(); foreach ($f in $blocked) { if (Test-Path (Join-Path $dir $f)) { $found += $f } };" ^
        "if ($found.Count -gt 0) { Write-Host '[DeFlux] ERROR: M1 runtime directory contains legacy artifacts:' ($found -join ', '); exit 1 };" ^
        "if (-not (Test-Path $dir)) { Write-Host '[DeFlux] ERROR: M1 runtime directory missing:' $dir; exit 1 };" ^
        "Write-Host '[DeFlux] M1 runtime directory guard: PASS'; exit 0"
    if errorlevel 1 (
        echo [DeFlux] M1 runtime directory verification failed.
        exit /b 1
    )
)

echo [DeFlux] Build succeeded. Binaries in "%OUT_DIR%"
if /I "%BUILD_SET%"=="M1" (
    echo [DeFlux] Legacy runtime was NOT rebuilt. Use "build.cmd %CONFIGURATION% legacy" only when explicitly needed.
    echo [DeFlux] Run tests from: "%OUT_DIR%"
)
exit /b 0
