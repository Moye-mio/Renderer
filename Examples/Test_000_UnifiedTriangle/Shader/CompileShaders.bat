@echo off
REM ============================================================================
REM 010_UnifiedTriangle / Shader / CompileShaders.bat
REM 把 .vert / .frag 编译为 .spv（用于 Vulkan 后端）。
REM 依赖：Vulkan SDK 中的 glslc。
REM 注意：本脚本不输出反射 JSON；如需反射可改用 ../../Tools/compile_shader.bat。
REM ============================================================================

setlocal
pushd "%~dp0"
glslc triangle.vert -o triangle.vert.spv
glslc triangle.frag -o triangle.frag.spv
popd
endlocal
