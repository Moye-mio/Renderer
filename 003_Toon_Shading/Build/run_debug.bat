@echo off
REM ============================================================================
REM 003_Toon_Shading/Build/run_debug.bat
REM   启动 x64\Debug\003_Toon_Shading.exe
REM   未指定 --backend 时程序默认 OpenGL。
REM
REM 用法：
REM   003_Toon_Shading\Build\run_debug.bat
REM   003_Toon_Shading\Build\run_debug.bat --backend=vk
REM   003_Toon_Shading\Build\run_debug.bat --backend=gl
REM ============================================================================

setlocal

set "SCRIPT_DIR=%~dp0"
set "ROOT=%SCRIPT_DIR%..\.."
set "PROJ=%SCRIPT_DIR%.."
set "EXE=%ROOT%\x64\Debug\003_Toon_Shading.exe"

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
