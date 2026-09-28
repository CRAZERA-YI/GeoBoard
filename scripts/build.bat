@echo off
rem ---------------------------------------------------------------------------
rem  GeoBoard build script (Windows + Qt5 + MSVC).
rem
rem  Why this file exists: the project workflow requires reproducible build
rem  commands, but the qmake + vcvars + nmake sequence is easy to get wrong
rem  (vcvars must be loaded in the SAME process, and the whole chain must be
rem  passed to cmd as one string). The commands verified during the environment
rem  phase are frozen here so anyone can rebuild without guessing.
rem
rem  NOTE: this file is intentionally pure ASCII. A .bat containing UTF-8
rem  Chinese text is read by cmd.exe using the OEM code page (GBK/936 on
rem  Simplified Chinese Windows), which mangles multi-byte characters and
rem  breaks command parsing. Keep it ASCII, or run "chcp 65001" beforehand.
rem
rem  Usage (run from the project root):
rem    scripts\build.bat            build the Release application
rem    scripts\build.bat test       build and run the geometry self-test
rem    scripts\build.bat json       build and run the file round-trip self-test
rem    scripts\build.bat render     render the canvas offscreen to docs\screenshot-render.png
rem    scripts\build.bat interact   build and run the synthetic-mouse interaction self-test
rem    scripts\build.bat shot       render the whole main window offscreen to docs\screenshot-window.png
rem    scripts\build.bat arcprobe   rebuild the arc-construction diagnostic probe (interactive replay)
rem    scripts\build.bat package    build Release + windeployqt into dist\
rem    scripts\build.bat clean      remove all build directories
rem
rem  Qt / MSVC locations come from QTSKILL_QT_DIR and QTSKILL_VCVARS when set,
rem  otherwise from the defaults below.
rem ---------------------------------------------------------------------------

setlocal enabledelayedexpansion

set "PROJECT_ROOT=%~dp0.."
set "QT_DIR=D:\Qt\Qt5.12.12\5.12.12\msvc2017_64"
set "VCVARS=D:\Visual Studio\Visual Studio 2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set "VS_REDIST_ROOT=D:\Visual Studio\Visual Studio 2022\Community\VC\Redist\MSVC"

if not "%QTSKILL_QT_DIR%"=="" set "QT_DIR=%QTSKILL_QT_DIR%"
if not "%QTSKILL_VCVARS%"=="" set "VCVARS=%QTSKILL_VCVARS%"
if not "%QTSKILL_VS_REDIST%"=="" set "VS_REDIST_ROOT=%QTSKILL_VS_REDIST%"

if not exist "%QT_DIR%\bin\qmake.exe" (
    echo [FAIL] qmake not found: "%QT_DIR%\bin\qmake.exe"
    echo        Set QTSKILL_QT_DIR to your Qt kit directory, for example:
    echo        set QTSKILL_QT_DIR=D:\Qt\Qt5.12.12\5.12.12\msvc2017_64
    exit /b 2
)
if not exist "%VCVARS%" (
    echo [FAIL] vcvars64.bat not found: "%VCVARS%"
    echo        Set QTSKILL_VCVARS to its full path.
    exit /b 2
)

set "ACTION=%~1"
if "%ACTION%"=="clean" goto :clean
if "%ACTION%"=="test" goto :test
if "%ACTION%"=="json" goto :json
if "%ACTION%"=="render" goto :render
if "%ACTION%"=="interact" goto :interact
if "%ACTION%"=="shot" goto :shot
if "%ACTION%"=="arcprobe" goto :arcprobe
if "%ACTION%"=="package" goto :package
goto :build

:build
echo === Building GeoBoard (Release) ===
set "BUILD_DIR=%PROJECT_ROOT%\build-release"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
pushd "%BUILD_DIR%"
call "%VCVARS%" >nul
if errorlevel 1 (echo [FAIL] failed to load MSVC environment & popd & exit /b 2)
"%QT_DIR%\bin\qmake.exe" "%PROJECT_ROOT%\GeoBoard.pro"
if errorlevel 1 (echo [FAIL] qmake failed & popd & exit /b 2)
nmake
if errorlevel 1 (echo [FAIL] nmake failed & popd & exit /b 2)
popd
echo [OK] artifact: %BUILD_DIR%\release\GeoBoard.exe
exit /b 0

:test
echo === Building and running the geometry self-test ===
set "TEST_DIR=%PROJECT_ROOT%\build-test"
if not exist "%TEST_DIR%" mkdir "%TEST_DIR%"
pushd "%TEST_DIR%"
call "%VCVARS%" >nul
if errorlevel 1 (echo [FAIL] failed to load MSVC environment & popd & exit /b 2)
"%QT_DIR%\bin\qmake.exe" "%PROJECT_ROOT%\tests\GeometrySmoke.pro"
if errorlevel 1 (echo [FAIL] qmake failed & popd & exit /b 2)
nmake
if errorlevel 1 (echo [FAIL] nmake failed & popd & exit /b 2)
popd
chcp 65001 >nul
"%TEST_DIR%\release\geometry_smoke.exe"
set "TEST_EXIT=%errorlevel%"
if "%TEST_EXIT%"=="0" (echo [OK] geometry self-test passed) else (echo [FAIL] self-test reported failures, exit %TEST_EXIT%)
exit /b %TEST_EXIT%

:json
echo === Building and running the file round-trip self-test ===
set "JSON_DIR=%PROJECT_ROOT%\build-json"
if not exist "%JSON_DIR%" mkdir "%JSON_DIR%"
pushd "%JSON_DIR%"
call "%VCVARS%" >nul
if errorlevel 1 (echo [FAIL] failed to load MSVC environment & popd & exit /b 2)
"%QT_DIR%\bin\qmake.exe" "%PROJECT_ROOT%\tests\JsonSmoke.pro"
if errorlevel 1 (echo [FAIL] qmake failed & popd & exit /b 2)
nmake
if errorlevel 1 (echo [FAIL] nmake failed & popd & exit /b 2)
popd
chcp 65001 >nul
"%JSON_DIR%\release\json_smoke.exe"
set "JSON_EXIT=%errorlevel%"
if "%JSON_EXIT%"=="0" (echo [OK] file round-trip self-test passed) else (echo [FAIL] round-trip reported failures, exit %JSON_EXIT%)
exit /b %JSON_EXIT%

:render
echo === Building and running the offscreen render dump ===
set "RENDER_DIR=%PROJECT_ROOT%\build-render"
if not exist "%RENDER_DIR%" mkdir "%RENDER_DIR%"
pushd "%RENDER_DIR%"
call "%VCVARS%" >nul
if errorlevel 1 (echo [FAIL] failed to load MSVC environment & popd & exit /b 2)
"%QT_DIR%\bin\qmake.exe" "%PROJECT_ROOT%\tests\RenderDump.pro"
if errorlevel 1 (echo [FAIL] qmake failed & popd & exit /b 2)
nmake
if errorlevel 1 (echo [FAIL] nmake failed & popd & exit /b 2)
popd
chcp 65001 >nul
"%RENDER_DIR%\release\render_dump.exe" "%PROJECT_ROOT%\docs\screenshot-render.png"
set "RENDER_EXIT=%errorlevel%"
if "%RENDER_EXIT%"=="0" (echo [OK] render dump written to docs\screenshot-render.png) else (echo [FAIL] render dump failed, exit %RENDER_EXIT%)
exit /b %RENDER_EXIT%

:interact
echo === Building and running the interaction self-test ===
set "INT_DIR=%PROJECT_ROOT%\build-interact"
if not exist "%INT_DIR%" mkdir "%INT_DIR%"
pushd "%INT_DIR%"
call "%VCVARS%" >nul
if errorlevel 1 (echo [FAIL] failed to load MSVC environment & popd & exit /b 2)
"%QT_DIR%\bin\qmake.exe" "%PROJECT_ROOT%\tests\InteractionSmoke.pro"
if errorlevel 1 (echo [FAIL] qmake failed & popd & exit /b 2)
nmake
if errorlevel 1 (echo [FAIL] nmake failed & popd & exit /b 2)
popd
chcp 65001 >nul
"%INT_DIR%\release\interaction_smoke.exe"
set "INT_EXIT=%errorlevel%"
if "%INT_EXIT%"=="0" (echo [OK] interaction self-test passed) else (echo [FAIL] interaction self-test reported failures, exit %INT_EXIT%)
exit /b %INT_EXIT%

:shot
echo === Rendering the whole main window offscreen ===
set "SHOT_DIR=%PROJECT_ROOT%\build-shot"
if not exist "%SHOT_DIR%" mkdir "%SHOT_DIR%"
pushd "%SHOT_DIR%"
call "%VCVARS%" >nul
if errorlevel 1 (echo [FAIL] failed to load MSVC environment & popd & exit /b 2)
"%QT_DIR%\bin\qmake.exe" "%PROJECT_ROOT%\tests\WindowShot.pro"
if errorlevel 1 (echo [FAIL] qmake failed & popd & exit /b 2)
nmake
if errorlevel 1 (echo [FAIL] nmake failed & popd & exit /b 2)
popd
chcp 65001 >nul
"%SHOT_DIR%\release\window_shot.exe" "%PROJECT_ROOT%\docs\screenshot-window.png"
if errorlevel 1 (echo [FAIL] window shot failed & exit /b 1)
echo [OK] window shot written to docs\screenshot-window.png
exit /b 0

:package
call "%~f0" build
if errorlevel 1 exit /b 2
echo === Collecting runtime into dist\ ===
set "DIST_DIR=%PROJECT_ROOT%\dist"
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%"
copy /y "%PROJECT_ROOT%\build-release\release\GeoBoard.exe" "%DIST_DIR%\" >nul
if errorlevel 1 (echo [FAIL] cannot copy GeoBoard.exe & exit /b 2)
"%QT_DIR%\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw "%DIST_DIR%\GeoBoard.exe"
if errorlevel 1 (echo [FAIL] windeployqt failed & exit /b 2)

rem --- MSVC runtime DLLs -----------------------------------------------------
rem  Measured fact: without this step the program loads msvcp140/vcruntime140
rem  from C:\Windows\System32, i.e. it only runs because the BUILD machine has
rem  the runtime installed. A target machine may not have it, so the
rem  redistributable DLLs are copied next to the exe. windeployqt on Qt 5.12
rem  does not necessarily do this for a v143 toolset.
set "CRT_DIR="
for /d %%v in ("%VS_REDIST_ROOT%\*") do (
    if exist "%%v\x64\Microsoft.VC143.CRT\msvcp140.dll" set "CRT_DIR=%%v\x64\Microsoft.VC143.CRT"
)
if "%CRT_DIR%"=="" (
    echo [WARN] Microsoft.VC143.CRT not found under "%VS_REDIST_ROOT%"
    echo        The package will rely on the target machine having the
    echo        VC++ 2015-2022 redistributable installed. Install it with
    echo        vc_runtime\vcredist_msvc2019_x64.exe on the target machine.
) else (
    copy /y "%CRT_DIR%\*.dll" "%DIST_DIR%\" >nul
    echo [OK] MSVC runtime DLLs copied from "%CRT_DIR%"
)

rem --- optional official runtime installer -----------------------------------
rem  Kept as a fallback for machines whose policy rejects local DLL copies.
if exist "%QT_DIR%\..\..\vcredist\vcredist_msvc2019_x64.exe" (
    if not exist "%DIST_DIR%\vc_runtime" mkdir "%DIST_DIR%\vc_runtime"
    copy /y "%QT_DIR%\..\..\vcredist\vcredist_msvc2019_x64.exe" "%DIST_DIR%\vc_runtime\" >nul
    echo [OK] official vcredist installer copied to vc_runtime\
)

rem --- end-user readme -------------------------------------------------------
rem  Delivered as README-run.txt. The NAME is ASCII on purpose: this .bat is
rem  parsed by cmd.exe using the OEM code page, so a Chinese file name inside it
rem  would break the script. The file CONTENT is Chinese ("how to run"), which
rem  is fine because it is a UTF-8 text file opened in Notepad.
rem  The template lives at the project root.
if exist "%PROJECT_ROOT%\README-run.txt" (
    copy /y "%PROJECT_ROOT%\README-run.txt" "%DIST_DIR%\README-run.txt" >nul
    echo [OK] end-user readme copied
)

echo [OK] deployable directory: %DIST_DIR%
exit /b 0

:arcprobe
echo === Rebuilding the arc-construction diagnostic probe ===
rem  Not a self-test: it replays real mouse events step by step, prints the state
rem  machine and the document after each step, and exports canvas PNGs.
rem  It exists because the "centre + two points arc" defect could NOT be found by
rem  reading code (geometry was correct, 40+ assertions were green) -- only by
rem  watching the event-by-event trace. Keep it around for the next such report.
set "PROBE_DIR=%PROJECT_ROOT%\build-arcprobe"
if not exist "%PROBE_DIR%" mkdir "%PROBE_DIR%"
pushd "%PROBE_DIR%"
call "%VCVARS%" >nul
if errorlevel 1 (echo [FAIL] failed to load MSVC environment & popd & exit /b 2)
"%QT_DIR%\bin\qmake.exe" "%PROJECT_ROOT%\tests\ArcCenterRepro.pro"
if errorlevel 1 (echo [FAIL] qmake failed & popd & exit /b 2)
nmake
if errorlevel 1 (echo [FAIL] nmake failed & popd & exit /b 2)
popd
chcp 65001 >nul
"%PROBE_DIR%\release\arc_repro.exe"
if errorlevel 1 (echo [FAIL] arc probe failed & exit /b 1)
echo [OK] arc probe finished (step log above, PNGs written to build-arcprobe)
exit /b 0

:clean
echo === Removing build directories ===
rem  debug/ and release/ are included on purpose: if qmake is ever run directly in
rem  the project root (by mistake), it scatters Makefile/debug/release there.
rem  Cleaning them keeps "qt_verify.py: build junk" reporting a clean tree.
for %%d in (build build-smoke build-test build-json build-render build-interact build-shot build-arcprobe build-release build-geom debug release dist) do (
    if exist "%PROJECT_ROOT%\%%d" (
        rmdir /s /q "%PROJECT_ROOT%\%%d"
        echo removed %%d
    )
)
exit /b 0
