@echo off
REM ============================================================================
REM Shader编译脚本：GLSL -> SPIR-V
REM 需要先安装 VulkanSDK，环境变量 VULKAN_SDK 指向 SDK 根目录
REM ============================================================================
set GLSLC="%VULKAN_SDK%\Bin\glslc.exe"
if not exist %GLSLC% (
    echo [ERROR] VulkanSDK not found. Please install and set VULKAN_SDK env var.
    exit /b 1
)

cd /d %~dp0
%GLSLC% triangle.vert -o triangle.vert.spv
if errorlevel 1 goto fail
%GLSLC% triangle.frag -o triangle.frag.spv
if errorlevel 1 goto fail

echo [OK] Shaders compiled.
exit /b 0

:fail
echo [FAIL] Shader compile failed.
exit /b 1
