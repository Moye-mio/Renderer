@echo off
REM ============================================================================
REM Tools/check_no_backend_headers.bat
REM 静态扫描包装器：业务工程（Examples/* 与 001_Reflective_shadow_map）的
REM 后端解耦边界守护脚本。
REM
REM 实际扫描逻辑由 Tools/check_no_backend_headers.py 实现（规则更全、
REM 错误信息更友好）。本 bat 仅做"找 Python 解释器"的薄包装。
REM
REM 用法：
REM   check_no_backend_headers.bat                      # 默认目标
REM   check_no_backend_headers.bat <dir1> [<dir2> ...]  # 指定扫描目标
REM ============================================================================

setlocal
set "HERE=%~dp0"
set "PY=%HERE%check_no_backend_headers.py"

REM 优先使用 PYTHON 环境变量；否则尝试 py launcher / python
if defined PYTHON (
    "%PYTHON%" "%PY%" %*
    goto :end
)
where py >nul 2>nul
if %errorlevel%==0 (
    py -3 "%PY%" %*
    goto :end
)
where python >nul 2>nul
if %errorlevel%==0 (
    python "%PY%" %*
    goto :end
)
echo [check] ERROR: cannot find Python interpreter; please install Python 3 or set %%PYTHON%%.
exit /b 2

:end
endlocal
exit /b %errorlevel%
