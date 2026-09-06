@echo off
REM ============================================================================
REM 006_Dynamic_Diffuse_GI/Build/run_debug.bat
REM   启动 x64\Debug\006_Dynamic_Diffuse_GI.exe
REM   未指定 --backend 时程序默认 Vulkan（rayQuery 需要 VK）。
REM
REM 用法：
REM   006_Dynamic_Diffuse_GI\Build\run_debug.bat
REM   006_Dynamic_Diffuse_GI\Build\run_debug.bat --backend=vk
REM   006_Dynamic_Diffuse_GI\Build\run_debug.bat --no-gui --screenshot-at=3
REM ============================================================================

setlocal

set "SCRIPT_DIR=%~dp0"
set "ROOT=%SCRIPT_DIR%..\.."
set "PROJ=%SCRIPT_DIR%.."
set "EXE=%ROOT%\x64\Debug\006_Dynamic_Diffuse_GI.exe"

if not exist "%EXE%" (
    echo [run_debug] 找不到: %EXE%
    echo [run_debug] 请先运行: "%SCRIPT_DIR%build_debug.bat"
    exit /b 1
)

echo [run_debug] %EXE% %*
echo.

pushd "%PROJ%"
"%EXE%" %*
set "ERR=%ERRORLEVEL%"
popd
exit /b %ERR%
