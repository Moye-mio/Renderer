@echo off
REM ============================================================================
REM tools/compile_shader.bat
REM   把一份 .glsl 同时产出：
REM     1) <name>.spv         —— 给 Vulkan 后端 vkCreateShaderModule 使用
REM     2) <name>.reflect.json —— 由 spirv-cross --reflect 生成的反射信息
REM     3) <name>.glsl 原文件 —— 供 OpenGL 后端 glShaderSource 使用（直接拷贝）
REM
REM 用法：
REM   compile_shader.bat path\to\shader.vert
REM   compile_shader.bat path\to\shader.frag
REM
REM 依赖：
REM   - glslc       (Vulkan SDK)
REM   - spirv-cross (Vulkan SDK)
REM ============================================================================

setlocal
if "%~1"=="" (
    echo [compile_shader] usage: %~nx0 ^<glsl-file^>
    exit /b 1
)

set "INPUT=%~1"
set "BASENAME=%~dpn1"
set "EXT=%~x1"

REM 简单根据扩展名推断 stage（与 glslc 默认推断一致）
echo [compile_shader] %INPUT%

glslc "%INPUT%" -o "%BASENAME%.spv"
if errorlevel 1 (
    echo [compile_shader] glslc failed
    exit /b 2
)

spirv-cross --reflect "%BASENAME%.spv" --output "%BASENAME%.reflect.json"
if errorlevel 1 (
    echo [compile_shader] spirv-cross --reflect failed
    exit /b 3
)

echo [compile_shader] OK -> %BASENAME%.spv  +  %BASENAME%.reflect.json
endlocal
exit /b 0
