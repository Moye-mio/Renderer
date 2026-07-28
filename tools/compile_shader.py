#!/usr/bin/env python3
"""tools/compile_shader.py
跨平台版本（与 compile_shader.bat 等价）：从 .glsl 同时产出
  - <name>.spv          供 Vulkan 后端 vkCreateShaderModule
  - <name>.reflect.json 由 spirv-cross --reflect 生成的反射信息
  - 原 .glsl 由 OpenGL 后端 glShaderSource 直接消费

依赖：
  - glslc       (Vulkan SDK)
  - spirv-cross (Vulkan SDK)

用法：
  python compile_shader.py path/to/shader.vert
"""
import os
import sys
import subprocess


def run(cmd):
    print("[compile_shader]", " ".join(cmd))
    return subprocess.call(cmd)


def main():
    if len(sys.argv) < 2:
        print("usage: compile_shader.py <glsl-file>")
        return 1

    src = sys.argv[1]
    base, _ = os.path.splitext(src)
    spv = base + ".spv"
    js = base + ".reflect.json"

    if run(["glslc", src, "-o", spv]) != 0:
        print("[compile_shader] glslc failed")
        return 2

    if run(["spirv-cross", "--reflect", spv, "--output", js]) != 0:
        print("[compile_shader] spirv-cross --reflect failed")
        return 3

    print("[compile_shader] OK ->", spv, "+", js)
    return 0


if __name__ == "__main__":
    sys.exit(main())
