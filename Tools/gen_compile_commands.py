#!/usr/bin/env python3
"""从仓库内各 .vcxproj + Directory.Build.props 生成 compile_commands.json，供 clangd 跳转。"""
from __future__ import annotations

import json
import os
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

CONFIG = "Debug"
PLATFORM = "x64"


def local_tag(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def condition_matches(cond: str | None) -> bool:
    if not cond or not cond.strip():
        return True
    needle = f"{CONFIG}|{PLATFORM}"
    if f"!='{needle}'" in cond or f'!="{needle}"' in cond:
        return False
    if f"=='{needle}'" in cond or f'=="{needle}"' in cond:
        return True
    if "TitusTracyEnable" in cond:
        return True
    return False


def split_msbuild_list(text: str) -> list[str]:
    items: list[str] = []
    for raw in text.replace("\n", ";").split(";"):
        item = raw.strip()
        if not item or item.startswith("%("):
            continue
        items.append(item)
    return items


def expand(text: str, props: dict[str, str]) -> str:
    prev = None
    out = text
    while prev != out:
        prev = out
        for key, value in props.items():
            out = out.replace(f"$({key})", value)
    return out


def text_of(parent: ET.Element, child_name: str) -> str | None:
    for child in parent:
        if local_tag(child.tag) == child_name and child.text:
            return child.text
    return None


def collect_clcompile_flags(root: ET.Element) -> tuple[list[str], list[str]]:
    includes: list[str] = []
    defines: list[str] = []
    for group in root.iter():
        if local_tag(group.tag) != "ItemDefinitionGroup":
            continue
        if not condition_matches(group.get("Condition")):
            continue
        for cl in group:
            if local_tag(cl.tag) != "ClCompile":
                continue
            inc = text_of(cl, "AdditionalIncludeDirectories")
            defs = text_of(cl, "PreprocessorDefinitions")
            if inc:
                includes.extend(split_msbuild_list(inc))
            if defs:
                defines.extend(split_msbuild_list(defs))
    return includes, defines


def collect_sources(root: ET.Element, project_dir: Path) -> list[Path]:
    sources: list[Path] = []
    for node in root.iter():
        if local_tag(node.tag) != "ClCompile":
            continue
        include = node.get("Include")
        if not include or include.startswith("%("):
            continue
        path = (project_dir / include).resolve()
        if path.suffix.lower() in {".cpp", ".cc", ".cxx", ".c"} and path.is_file():
            sources.append(path)
    return sources


def load_directory_build_props(repo: Path, props: dict[str, str]) -> tuple[list[str], list[str]]:
    includes: list[str] = []
    defines: list[str] = []
    for name in ("Directory.Build.props", "Directory.Build.targets"):
        path = repo / name
        if not path.is_file():
            continue
        root = ET.parse(path).getroot()
        for group in root.iter():
            if local_tag(group.tag) != "PropertyGroup":
                continue
            for child in group:
                tag = local_tag(child.tag)
                if child.text and tag not in props:
                    props[tag] = expand(child.text, props)
        inc, defs = collect_clcompile_flags(root)
        includes.extend(inc)
        defines.extend(defs)
    return includes, defines


def find_clang_cl() -> str:
    candidates = [
        os.environ.get("CLANG_CL"),
        r"C:\Program Files\LLVM\bin\clang-cl.exe",
        r"C:\Program Files (x86)\LLVM\bin\clang-cl.exe",
    ]
    for cand in candidates:
        if cand and Path(cand).is_file():
            return str(Path(cand))
    return "clang-cl.exe"


def posix(path: Path | str) -> str:
    return str(Path(path)).replace("\\", "/")


def unique(seq: list[str]) -> list[str]:
    seen: set[str] = set()
    out: list[str] = []
    for item in seq:
        key = item.replace("\\", "/").rstrip("/")
        if key in seen:
            continue
        seen.add(key)
        out.append(item)
    return out


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    sln_dir = str(repo) + os.sep
    vulkan = os.environ.get("VULKAN_SDK") or os.environ.get("VK_SDK_PATH") or ""

    props: dict[str, str] = {
        "MSBuildThisFileDirectory": sln_dir,
        "SolutionDir": sln_dir,
        "VULKAN_SDK": vulkan,
        "VK_SDK_PATH": vulkan,
    }
    extra_inc, extra_defs = load_directory_build_props(repo, props)
    extra_defs.extend(["UNICODE", "_UNICODE", "WIN32", "_WIN64"])

    clang_cl = find_clang_cl()
    entries: list[dict[str, object]] = []
    missing_sdk = not vulkan

    for vcxproj in sorted(repo.rglob("*.vcxproj")):
        if "Third-Party" in vcxproj.parts:
            continue
        project_dir = vcxproj.parent
        props["ProjectDir"] = str(project_dir) + os.sep
        root = ET.parse(vcxproj).getroot()
        inc_raw, def_raw = collect_clcompile_flags(root)
        includes = unique(
            [expand(x, props) for x in extra_inc + inc_raw + [str(project_dir)]]
        )
        defines = unique([expand(x, props) for x in extra_defs + def_raw])
        directory = posix(project_dir)

        for src in collect_sources(root, project_dir):
            args = [
                posix(clang_cl),
                "/nologo",
                "/c",
                "/std:c++latest",
                "/EHsc",
                "/Zc:__cplusplus",
                "/W0",
            ]
            for d in defines:
                args.append(f"/D{d}")
            for inc in includes:
                if inc:
                    args.extend(["/I", posix(inc)])
            args.append(posix(src))
            entries.append(
                {
                    "directory": directory,
                    "file": posix(src),
                    "arguments": args,
                }
            )

    out = repo / "compile_commands.json"
    out.write_text(json.dumps(entries, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {out} ({len(entries)} files)")
    if missing_sdk:
        print(
            "warning: VULKAN_SDK / VK_SDK_PATH unset; Vulkan headers will not resolve.",
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
