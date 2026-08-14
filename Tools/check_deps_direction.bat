@echo off
REM ============================================================================
REM Tools/check_deps_direction.bat
REM 校验"依赖方向单向"（需求 9.4 / 10.4）：
REM   RendererInterface  →  { RendererGL, RendererVK, RendererCore, Platform }
REM   RendererGL         →  { RendererCore, Platform }
REM   RendererVK         →  { RendererCore, Platform }
REM   RendererCore       →  Platform 仅限 IWindow 接口（本脚本宽容，不强制）
REM   Platform           →  RendererCore（IWindow 接口）
REM
REM 即在以下源代码目录里禁止出现 #include 反向引用：
REM   * RendererCore/*.h/.cpp  禁止 include "RendererGL/", "RendererVK/", "RendererInterface/"
REM   * RendererGL/*.h/.cpp    禁止 include "RendererVK/", "RendererInterface/"
REM   * RendererVK/*.h/.cpp    禁止 include "RendererGL/", "RendererInterface/"
REM   * Platform/*.h/.cpp      禁止 include "RendererGL/", "RendererVK/", "RendererInterface/"
REM                              （允许 "RendererCore/"，供 IWindow）
REM   * AssetLoader/*.h/.cpp   禁止 include "RendererGL/", "RendererCore/", "RendererVK/", "RendererInterface/", "Platform/"
REM
REM 命中即以非零状态退出，可用作 RendererInterface 的 PreBuild CI 校验。
REM
REM 用法：Tools\check_deps_direction.bat
REM ============================================================================

setlocal EnableDelayedExpansion
set "ROOT=%~dp0.."
set "FOUND=0"

call :scan "RendererCore"      "RendererGL/"      "RendererVK/"  "RendererInterface/"
call :scan "RendererGL"         "RendererVK/"      "RendererInterface/"
call :scan "RendererVK"         "RendererGL/"      "RendererInterface/"
REM Platform 允许 include RendererCore（IWindow 接口），见文件头依赖方向说明。
call :scan "Platform"           "RendererGL/"      "RendererVK/" "RendererInterface/"
call :scan "AssetLoader"        "RendererGL/"      "RendererCore/" "RendererVK/" "RendererInterface/" "Platform/"

if "%FOUND%"=="1" (
    echo [check_deps_direction] FAILED
    exit /b 1
)
echo [check_deps_direction] PASS
endlocal
exit /b 0

:scan
REM %1 = scope dir (relative to ROOT), %2.. = forbidden include prefixes
set "DIR=%ROOT%\%~1"
echo [check_deps_direction] scope: %DIR%
if not exist "%DIR%" (
    echo [warn] scope not found: %DIR%
    goto :eof
)
shift
set "PATTERNS="
:loop_args
if "%~1"=="" goto :do_scan
set "P=%~1"
set "PATTERNS=!PATTERNS! /C:"#include[ ]*\"!P!" /C:"#include[ ]*\"\\.\\./!P!" /C:"#include[ ]*\"\\.\\./\\.\\./!P!""
shift
goto loop_args
:do_scan
if "!PATTERNS!"=="" goto :eof
for /R "%DIR%" %%F in (*.h *.hpp *.hh *.hxx *.cpp *.cc *.cxx) do (
    findstr /R !PATTERNS! "%%F" >nul
    if !errorlevel!==0 (
        echo [FAIL] reverse-direction include found in: %%F
        set "FOUND=1"
    )
)
goto :eof
