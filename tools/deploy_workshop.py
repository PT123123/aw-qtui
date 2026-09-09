#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
deploy_workshop.py —— 把正式 release 发布到本地 workshop 目录。

逻辑（复用 make_zip.py 的版本处理）：
    1. 读取 CMakeLists.txt 当前版本，patch +1（如 0.1.4 -> 0.1.5）；
    2. 把 build/dist/（just release 的产物）整目录拷贝到
       c:/workshop/aw-qtui-<ver>；
    3. 把新版本写回 CMakeLists.txt（下次运行自动继续 +1）。

用法（由 justfile 的 deploy-workshop 目标调用，需先 just release）：
    python tools/deploy_workshop.py
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import make_zip  # noqa: E402  复用 parse_version_from_cmake / bump_patch / write_back_version

BUILD_DIR = make_zip.BUILD_DIR
SRC_DIR = os.path.join(BUILD_DIR, "dist")
WORKSHOP = "c:/workshop"
APP_NAME = "aw-qtui"


def main() -> None:
    base_ver = make_zip.parse_version_from_cmake() or "0.1.0"
    ver = make_zip.bump_patch(base_ver)
    print(f"[ver] 版本号: {base_ver} -> {ver}（部署成功后写回 CMakeLists.txt）")

    if not os.path.isdir(SRC_DIR):
        raise SystemExit(f"源目录不存在：{SRC_DIR}\n请先执行 just release")

    target = os.path.join(WORKSHOP, f"{APP_NAME}-{ver}")
    if os.path.isdir(target):
        shutil.rmtree(target)
    shutil.copytree(SRC_DIR, target)

    make_zip.write_back_version(base_ver, ver)

    print("[done] deploy-workshop 完成:")
    print(f"  {target}")


if __name__ == "__main__":
    main()