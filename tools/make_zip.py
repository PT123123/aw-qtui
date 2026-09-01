#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
make_zip.py —— aw-qtui 发布打包（取代原 release.ps1 的打包段）。

从 build/ 组装 dist/aw-qtui-<ver>-win64，剔除 CMake/Ninja 中间产物，
用标准库 zipfile 打成同名 .zip。版本号默认在 CMakeLists.txt 当前版本上
patch +0.01 自动递增并写回；也可用 --version 指定（不自动加、不写回）。

用法（由 Makefile 的 dist 目标调用）：
    python tools/make_zip.py                 # 默认：版本 +0.01 自动递增并写回
    python tools/make_zip.py --version 0.2.0 # 指定版本号，不写回
    python tools/make_zip.py --skip-server   # 打包不含服务端（server/ 不被纳入）

依赖：仅 Python 标准库（无第三方包，无需 zip.exe / 7z）。
"""
import argparse
import os
import re
import shutil
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_DIR = os.path.join(ROOT, "build")
CMAKE_LISTS = os.path.join(ROOT, "CMakeLists.txt")
README = os.path.join(ROOT, "README.md")

# 打包时需剔除的目录/文件（与旧 release.ps1 保持一致）
EXCLUDE_NAMES = {
    "CMakeCache.txt",
    "CMakeFiles",
    "cmake_install.cmake",
    "build.ninja",
    "CTestTestfile.cmake",
    "Makefile",
    "awqtui_autogen",
    "server-src",          # cargo 暂存，非发布内容
    ".qt",                 # windeployqt 部署支撑 cmake，非发布内容
}
EXCLUDE_SUFFIXES = (".obj", ".ilk", ".pdb")
EXCLUDE_PREFIXES = (".ninja",)


def parse_version_from_cmake() -> str | None:
    with open(CMAKE_LISTS, "r", encoding="utf-8-sig") as f:
        text = f.read()
    m = re.search(r'project\(\s*aw-qtui\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)', text)
    return m.group(1) if m else None


def bump_patch(ver: str) -> str:
    parts = ver.split(".")
    if len(parts) != 3:
        raise ValueError(f"无法解析版本号（需 X.Y.Z）: {ver}")
    parts[2] = str(int(parts[2]) + 1)
    return ".".join(parts)


def write_back_version(old: str, new: str) -> None:
    with open(CMAKE_LISTS, "r", encoding="utf-8-sig") as f:
        text = f.read()
    # 仅替换 project(aw-qtui VERSION <old> 这一处，避免误伤其他行
    pat = re.compile(r'(project\(\s*aw-qtui\s+VERSION\s+)' + re.escape(old) + r'(\b)')
    new_text, n = pat.subn(lambda mm: mm.group(1) + new + mm.group(2), text, count=1)
    if n == 0:
        print(f"[warn] 未在 CMakeLists.txt 中找到版本 {old}，跳过写回")
        return
    with open(CMAKE_LISTS, "w", encoding="utf-8") as f:
        f.write(new_text)
    print(f"[ok] 已写回 CMakeLists.txt: VERSION {old} -> {new}")


def should_exclude(name: str) -> bool:
    if name in EXCLUDE_NAMES:
        return True
    if name.lower().endswith(EXCLUDE_SUFFIXES):
        return True
    if any(name.startswith(p) for p in EXCLUDE_PREFIXES):
        return True
    return False


def assemble(dist_dir: str, skip_server: bool) -> None:
    if not os.path.isdir(BUILD_DIR):
        raise SystemExit(f"build/ 不存在，请先执行 make release：{BUILD_DIR}")
    # 客户端是必带产物
    if not os.path.isfile(os.path.join(BUILD_DIR, "awqtui.exe")):
        raise SystemExit("build/awqtui.exe 缺失，请先执行 make release")
    # 服务端默认必带；缺则明确报错，绝不静默产出不带服务端的包
    server_exe = os.path.join(BUILD_DIR, "server", "aw-inbox-rust.exe")
    if not skip_server and not os.path.isfile(server_exe):
        raise SystemExit(
            "默认 release 必须包含服务端（build/server/aw-inbox-rust.exe 缺失）。\n"
            "请先执行 make release（默认带服务端），或显式传 --skip-server 发布纯客户端包。"
        )

    if os.path.isdir(dist_dir):
        shutil.rmtree(dist_dir)
    os.makedirs(dist_dir)

    for entry in sorted(os.listdir(BUILD_DIR)):
        if should_exclude(entry):
            continue
        if skip_server and entry == "server":
            continue
        src = os.path.join(BUILD_DIR, entry)
        dst = os.path.join(dist_dir, entry)
        if os.path.isdir(src):
            shutil.copytree(src, dst)
        else:
            shutil.copy2(src, dst)

    # 附带 README
    if os.path.isfile(README):
        shutil.copy2(README, os.path.join(dist_dir, "README.md"))

    print(f"[ok] 已组装发布目录: {dist_dir}")


def zip_dir(dist_dir: str, zip_path: str) -> None:
    if os.path.isfile(zip_path):
        os.remove(zip_path)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for base, _dirs, files in os.walk(dist_dir):
            for fn in files:
                fp = os.path.join(base, fn)
                arcname = os.path.relpath(fp, os.path.dirname(dist_dir))
                zf.write(fp, arcname)
    print(f"[ok] 已打包: {zip_path}")


def main() -> None:
    ap = argparse.ArgumentParser(description="aw-qtui 发布打包")
    ap.add_argument("--version", help="指定版本号（不自动 +0.01、不写回 CMakeLists.txt）")
    ap.add_argument("--skip-server", action="store_true", help="打包不含服务端")
    args = ap.parse_args()

    base_ver = parse_version_from_cmake()
    if args.version:
        ver = args.version.strip().lstrip("v")
        print(f"[ver] 使用指定版本: {ver}（不写回 CMakeLists.txt）")
    else:
        if not base_ver:
            base_ver = "0.1.0"
        ver = bump_patch(base_ver)
        print(f"[ver] 版本号: {base_ver} -> {ver}（打包成功后写回 CMakeLists.txt）")

    dist_name = f"aw-qtui-{ver}-win64"
    dist_dir = os.path.join(ROOT, "dist", dist_name)
    zip_path = os.path.join(ROOT, "dist", dist_name + ".zip")

    assemble(dist_dir, args.skip_server)
    zip_dir(dist_dir, zip_path)

    if not args.version:
        # 打包成功才消耗版本号；失败在上面已 SystemExit，不会走到这里
        write_back_version(base_ver, ver)

    print("[done] 发布完成:")
    print(f"  {dist_dir}")
    print(f"  {zip_path}")


if __name__ == "__main__":
    main()
