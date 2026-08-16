@echo off
setlocal
rem ============================================================
rem  Park3D standalone executable packaging (Development / Win64)
rem  Output: Package\Windows\Park3D.exe
rem
rem  UBT Build.bat only produces UnrealEditor-Park3D.dll (a module).
rem  A standalone exe needs cook + stage, so this drives UAT.
rem
rem  ASCII ONLY. A .bat cannot control the code page it is launched
rem  under; with chcp 65001 active, CP949 Korean trail bytes decode
rem  as |, &, > and turn comments into commands. Korean notes live in
rem  Docs/ instead.
rem ============================================================

set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "RUNUAT=%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat"
set "PROJECT=%~dp0Park3D\Park3D.uproject"
set "ARCHIVE=%~dp0Package"
set "LOG=%~dp0_workspace\package_build.log"

if not exist "%RUNUAT%" (
    echo [FAILED] RunUAT not found: "%RUNUAT%"
    echo          Fix UE_ROOT in this file if the engine moved or changed version.
    exit /b 1
)

rem A running package locks the staged files: staging then fails or leaves a stale exe.
tasklist /FI "IMAGENAME eq Park3D.exe" 2>nul | find /I "Park3D.exe" > nul
if not errorlevel 1 (
    echo [FAILED] Park3D.exe is running. Close it and run again.
    exit /b 1
)

echo [PACKAGE] start - Development / Win64
echo [PACKAGE] log: "%LOG%"
echo.

rem Progress percentage is drawn by BuildProgress.ps1, which also writes the full
rem output to the log (a .bat has no tee). The percentages come from lines UAT
rem already prints: "[n/m] Compile ..." while building and "Cooked packages N ...
rem Total T" while cooking. Cook's Total grows as dependencies are discovered, so
rem the percentage can step backwards - that is expected, not a stall.
rem
rem The pipe makes ERRORLEVEL reflect the pipeline, not UAT. That is fine here:
rem success is judged below by the log marker and the artifact, never by exit code.
if not exist "%~dp0BuildProgress.ps1" (
    echo [FAILED] BuildProgress.ps1 not found next to this file.
    exit /b 1
)

call "%RUNUAT%" BuildCookRun ^
    -project="%PROJECT%" ^
    -noP4 -platform=Win64 -clientconfig=Development ^
    -cook -build -stage -pak ^
    -archive -archivedirectory="%ARCHIVE%" 2>&1 | powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0BuildProgress.ps1" -LogPath "%LOG%"

rem RunUAT's wrapper can return 0 even when the build failed (e.g. cook errors),
rem so judge by the log's success marker and by the artifact, not the exit code.
findstr /C:"AutomationTool exiting with ExitCode=0" "%LOG%" > nul
if errorlevel 1 (
    echo [FAILED] UAT did not finish successfully.
    echo.
    echo ---- error lines from the log ----
    findstr /C:"BUILD FAILED" /C:"ERROR:" /C:"Error_" /C:"error(s)" "%LOG%"
    echo ----------------------------------
    echo Full log: "%LOG%"
    exit /b 1
)

if not exist "%ARCHIVE%\Windows\Park3D.exe" (
    echo [FAILED] Artifact missing: "%ARCHIVE%\Windows\Park3D.exe"
    echo Full log: "%LOG%"
    exit /b 1
)

rem Reference data (Save\3D, Save\Config) is not cooked - it is plain JSON read at
rem runtime. Park3DDataPaths::GetSaveRootDir looks for <Stage>\Save when
rem ProjectDir()\Save is absent, so stage it next to the exe. Without this the
rem package starts but has no car catalog, no presets, no cameras.
rem Sim is excluded: replay frames are produced per run, not input data.
robocopy "%~dp0Park3D\Save" "%ARCHIVE%\Windows\Save" /MIR /XD Sim /NFL /NDL /NJH /NJS /NP > nul
if errorlevel 8 (
    echo [FAILED] Could not stage Save data to "%ARCHIVE%\Windows\Save"
    exit /b 1
)

echo [OK] "%ARCHIVE%\Windows\Park3D.exe"
exit /b 0
