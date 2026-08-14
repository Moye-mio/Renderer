#!/usr/bin/env python3
"""Tools/check_no_backend_headers.py
静态扫描：在指定目录的 *.h/*.cpp 中查找是否包含
  <vulkan/...>, <GL/...>, <glad/...>, <GLFW/glfw3.h>
命中即以非零状态退出，可被 CI 直接调用。

STRICT 模式（自动启用：路径中包含 "Examples" 或 "001_Reflective_shadow_map"）
额外校验：
  - "Renderer/...", "RendererCore/...", "RendererVK/...", "Platform/..." 等
    任何后端模块字面 include（含 "../" 前缀）；
  - 旧 GL 链路头：Interface.h / IRenderPass.h / ShaderProgram.h /
    ResourceManager.h / GLUtils.h / Common.h / IGameObject.h；
  - `TitusGraphics::` 旧命名空间字面；
  - 原生 GL 直调 `glXxx(...)`（仅 .cpp/.cc/.cxx）。

默认扫描：
  - RendererCore/
  - Examples/Test_000_UnifiedTriangle/
  - 001_Reflective_shadow_map/
"""
import os
import re
import sys

# 共通规则：所有目标都禁止裸 SDK 头
COMMON_PATTERNS = [
    re.compile(r'#\s*include\s*<\s*vulkan/'),
    re.compile(r'#\s*include\s*<\s*GL/'),
    re.compile(r'#\s*include\s*<\s*glad'),
    re.compile(r'#\s*include\s*<\s*GLFW/glfw3\.h\s*>'),
    re.compile(r'#\s*include\s*"\s*glfw3\.h\s*"'),
]

# STRICT 规则：业务工程禁止任何后端模块字面 include
STRICT_INCLUDE_PATTERNS = [
    re.compile(r'#\s*include\s*"(?:\.\./)*RendererGL/'),
    re.compile(r'#\s*include\s*"(?:\.\./)*RendererCore/'),
    re.compile(r'#\s*include\s*"(?:\.\./)*RendererVK/'),
    re.compile(r'#\s*include\s*"(?:\.\./)*Platform/'),
    # 旧 GL 链路具体头（即便它们位于 include path 中也禁止）
    re.compile(r'#\s*include\s*"(?:\.\./)*Interface\.h"'),
    re.compile(r'#\s*include\s*"(?:\.\./)*IRenderPass\.h"'),
    re.compile(r'#\s*include\s*"(?:\.\./)*ShaderProgram\.h"'),
    re.compile(r'#\s*include\s*"(?:\.\./)*ResourceManager\.h"'),
    re.compile(r'#\s*include\s*"(?:\.\./)*GLUtils\.h"'),
    re.compile(r'#\s*include\s*"(?:\.\./)*Common\.h"'),
    re.compile(r'#\s*include\s*"(?:\.\./)*IGameObject\.h"'),
]

# STRICT 规则：禁止 TitusGraphics:: 旧命名空间
STRICT_NAMESPACE_PATTERN = re.compile(r'\bTitusGraphics::')

# STRICT 规则：禁止原生 GL 函数直调（仅 .cpp/.cc/.cxx）
# 形如 `glClear(`、`glBindFramebuffer(`、`glDispatchCompute(` 等
STRICT_GL_CALL_PATTERN = re.compile(r'(?<![A-Za-z_])gl[A-Z][A-Za-z]+\s*\(')

EXTS = {".h", ".hpp", ".hh", ".hxx", ".cpp", ".cc", ".cxx"}
CPP_EXTS = {".cpp", ".cc", ".cxx"}


def _is_strict(target_dir):
    norm = target_dir.replace("\\", "/")
    return ("Examples" in norm) or ("001_Reflective_shadow_map" in norm)


def scan(target_dir):
    failed = []
    strict = _is_strict(target_dir)
    for root, _dirs, files in os.walk(target_dir):
        for f in files:
            ext = os.path.splitext(f)[1].lower()
            if ext not in EXTS:
                continue
            path = os.path.join(root, f)
            try:
                with open(path, "r", encoding="utf-8", errors="ignore") as fp:
                    for ln, line in enumerate(fp, 1):
                        for p in COMMON_PATTERNS:
                            if p.search(line):
                                failed.append((path, ln, "SDK header", line.rstrip()))
                        if strict:
                            for p in STRICT_INCLUDE_PATTERNS:
                                if p.search(line):
                                    failed.append((path, ln, "backend include", line.rstrip()))
                            if STRICT_NAMESPACE_PATTERN.search(line):
                                failed.append((path, ln, "TitusGraphics::", line.rstrip()))
                            if ext in CPP_EXTS and STRICT_GL_CALL_PATTERN.search(line):
                                failed.append((path, ln, "raw glXxx() call", line.rstrip()))
            except OSError:
                pass
    return failed


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.dirname(here)
    targets = argv[1:] if len(argv) > 1 else [
        os.path.join(repo, "RendererCore"),
        os.path.join(repo, "Examples", "Test_000_UnifiedTriangle"),
        os.path.join(repo, "Examples", "Test_002_RayQueryHello"),
        os.path.join(repo, "001_Reflective_shadow_map"),
    ]

    any_fail = False
    for t in targets:
        print(f"[check] scanning {t}{' [STRICT]' if _is_strict(t) else ''}")
        if not os.path.isdir(t):
            print(f"[warn] target not found: {t}")
            continue
        fails = scan(t)
        for p, ln, kind, line in fails:
            print(f"[FAIL] {p}:{ln}: ({kind}) {line}")
            any_fail = True

    if any_fail:
        print("[check] FAILED")
        return 1
    print("[check] PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
