@echo off
REM ============================================================================
REM 004_Anti_Aliasing/Build/build_debug.bat
REM   Debug | x64，并显式开启 Tracy（TitusTracyEnable=true）
REM
REM 用法（在任意目录均可）：
REM   004_Anti_Aliasing\Build\build_debug.bat
REM ============================================================================

setlocal EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
REM 本脚本在 004_Anti_Aliasing/Build/，仓库根在上两级
set "ROOT=%SCRIPT_DIR%..\.."
set "SLN=%ROOT%\TitusGLRenderer.sln"
set "CONFIG=Debug"
REM 解决方案只有 Debug|Any CPU；各工程再映射到 Debug|x64
set "SLN_PLATFORM=Any CPU"
set "OUT_PLATFORM=x64"

if not exist "%SLN%" (
    echo [build_debug] 找不到解决方案: %SLN%
    exit /b 1
)

if not exist "%ROOT%\Third-Party\tracy\public\tracy\Tracy.hpp" (
    echo [build_debug] 未找到 Tracy 子模块。请先执行:
    echo     git submodule update --init --recursive
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [build_debug] 找不到 vswhere.exe，请确认已安装 Visual Studio 2022。
    exit /b 1
)

for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set "MSBUILD=%%I"
)

if not defined MSBUILD (
    echo [build_debug] 找不到 MSBuild。请安装 VS 2022 及 MSBuild 组件。
    exit /b 1
)

echo [build_debug] MSBuild : %MSBUILD%
echo [build_debug] Solution: %SLN%
echo [build_debug] Target  : 004_Anti_Aliasing ^| %CONFIG% ^| "%SLN_PLATFORM%" -^> %OUT_PLATFORM% ^| Tracy=ON
echo.

"%MSBUILD%" "%SLN%" ^
    /t:004_Anti_Aliasing ^
    /p:Configuration=%CONFIG% ^
    /p:Platform="%SLN_PLATFORM%" ^
    /p:TitusTracyEnable=true ^
    /m ^
    /v:minimal ^
    /nologo
if errorlevel 1 (
    echo.
    echo [build_debug] FAILED
    exit /b 1
)

set "OUT=%ROOT%\%OUT_PLATFORM%\%CONFIG%\004_Anti_Aliasing.exe"
echo.
echo [build_debug] OK
echo [build_debug] 输出: %OUT%
echo [build_debug] 运行: "%SCRIPT_DIR%run_debug.bat"
endlocal
exit /b 0
