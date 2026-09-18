#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
deploy_workshop.py —— 把正式 release 发布到本地 workshop 目录，并完成「新旧版本交接」。

逻辑（复用 make_zip.py 的版本处理）：
    1. 读取 CMakeLists.txt 当前版本，patch +1（如 0.1.6 -> 0.1.7）；
    2. 把 build/dist/（just release 的产物）整目录拷贝到
       c:/workshop/aw-qtui-<ver>；
    3. 把新版本写回 CMakeLists.txt（下次运行自动继续 +1）；
    4. 【交接】若检测到有实例在跑且比新版更旧，就把新版 exe 拉起来：
       新版启动后自己会走单实例仲裁（见 src/singleinstance.h）—— 请求旧版优雅让位并接管。
       旧版若 ≤0.1.6（没有让位通道），新版会明确提示需要手动从托盘退出一次。

为什么是「拉起新版」而不是「脚本去关旧版」：
    关闭旧实例的正确做法是让它自己走 flush + quit 的优雅路径。脚本从外面只能发 WM_CLOSE，
    而客户端把 WM_CLOSE 定义成「最小化到托盘」（不会退出），硬杀又违反「不硬杀」的约定。
    所以把交接权交给新版程序——它知道怎么跟旧版谈判。

用法（由 justfile 的 deploy-workshop 目标调用，需先 just release）：
    python tools/deploy_workshop.py
"""
import ctypes
import os
import shutil
import subprocess
import sys
import time
from ctypes import wintypes

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import make_zip  # noqa: E402  复用 parse_version_from_cmake / bump_patch / write_back_version

BUILD_DIR = make_zip.BUILD_DIR
SRC_DIR = os.path.join(BUILD_DIR, "dist")
WORKSHOP = "c:/workshop"
APP_NAME = "aw-qtui"
EXE_NAME = "awqtui.exe"

# 客户端约定：锁文件在 %APPDATA%/aw-qtui/aw-qtui/awqtui.lock（QLockFile 文本格式，首行是 pid）。
# 与 src/singleinstance.cpp 的 SingleInstance::lockFilePath() 必须指向同一个文件。
LOCK_REL = os.path.join("aw-qtui", "aw-qtui", "awqtui.lock")


# ---------------- 运行中实例探测（只读，不打扰目标进程） ----------------

def _process_image(pid):
    """取进程镜像路径；进程不存在或无权限时返回空串。低权限查询，不修改目标进程。"""
    if pid <= 0:
        return ""
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.OpenProcess.restype = wintypes.HANDLE
    k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    k32.QueryFullProcessImageNameW.argtypes = [
        wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    k32.CloseHandle.argtypes = [wintypes.HANDLE]

    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    handle = k32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return ""
    try:
        size = wintypes.DWORD(1024)
        buf = ctypes.create_unicode_buffer(1024)
        if not k32.QueryFullProcessImageNameW(handle, 0, buf, ctypes.byref(size)):
            return ""
        return buf.value
    finally:
        k32.CloseHandle(handle)


def running_instance():
    """返回 (pid, exe_path)：检测到正在运行的 aw-qtui 客户端时给出；否则 (0, "")。"""
    appdata = os.environ.get("APPDATA")
    if not appdata:
        return 0, ""
    try:
        with open(os.path.join(appdata, LOCK_REL), "r", encoding="utf-8", errors="ignore") as fh:
            first = fh.readline().strip()
    except OSError:
        return 0, ""
    if not first.isdigit():
        return 0, ""
    pid = int(first)
    exe = _process_image(pid)
    # 确认确实是本程序：锁文件是陈旧的，pid 也可能已被系统复用
    if not exe or os.path.basename(exe).lower() != EXE_NAME:
        return 0, ""
    return pid, exe


def version_from_exe(exe_path):
    """从 <parent>/aw-qtui-<ver>/awqtui.exe 反推版本号；不是同族目录则返回空串。"""
    parent = os.path.basename(os.path.dirname(exe_path))
    prefix = APP_NAME + "-"
    if not parent.startswith(prefix):
        return ""
    return parent[len(prefix):].strip()


def _ver_tuple(ver):
    """'0.1.10' -> (0, 1, 10)：保证 0.1.10 大于 0.1.7（直接字符串比较会判反）。"""
    out = []
    for seg in ver.split("."):
        digits = ""
        for ch in seg:
            if ch.isdigit():
                digits += ch
            else:
                break
        out.append(int(digits) if digits else 0)
    return tuple(out)


# ---------------- 交接 ----------------

def ipc_capable(pid):
    """运行中的实例是否具备「交接通道」（即 ≥0.1.7 的构建）。

    判据：0.1.7 起实例在持锁时会写 awqtui.instance.json（pid + version + exe）。
    文件缺失或 pid 对不上，说明跑着的是更早的构建 —— 它没有 IPC，叫不动。
    """
    appdata = os.environ.get("APPDATA")
    if not appdata:
        return False
    try:
        with open(os.path.join(appdata, os.path.dirname(LOCK_REL), "awqtui.instance.json"),
                  "r", encoding="utf-8", errors="ignore") as fh:
            import json
            info = json.load(fh)
    except (OSError, ValueError):
        return False
    return int(info.get("pid", 0)) == pid


def handoff(new_ver, target):
    """拉起新版，由新版自己走单实例仲裁接管旧实例。返回是否确认接管成功。"""
    new_exe = os.path.join(target, EXE_NAME)
    if not os.path.isfile(new_exe):
        print(f"[handoff] 跳过：新版 exe 不存在 {new_exe}")
        return True

    pid, old_exe = running_instance()
    if not pid:
        print("[handoff] 未检测到正在运行的实例，不自动拉起（部署完成，按需自行启动）")
        return True

    old_ver = version_from_exe(old_exe) or "?"
    newer = True if old_ver == "?" else _ver_tuple(new_ver) > _ver_tuple(old_ver)

    print(f"[handoff] 检测到运行中的实例：pid={pid} v{old_ver}")
    print(f"          {old_exe}")
    if not newer:
        print(f"[handoff] 运行中的版本不旧于新版 v{new_ver} → 不拉起（无需交接）")
        return True

    if not ipc_capable(pid):
        # 老构建没有让位通道：拉起只会让它弹一个需要手点的提示框，不如在这里说清楚
        print(f"[handoff] 该实例是 v{old_ver}（≤0.1.6，没有自动交接通道）→ 不拉起，避免弹窗")
        print("          请从托盘退出旧版一次，然后启动：")
        print(f"          {new_exe}")
        print("          从这一版起，后续升级会由新版自动完成交接（无需手动关闭）。")
        return False

    print(f"[handoff] 拉起新版 v{new_ver}，由它请求 v{old_ver} 优雅让位…")
    # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP：脱离本脚本的控制台与进程组，
    # 脚本退出后新版继续存活。
    DETACHED_PROCESS = 0x00000008
    CREATE_NEW_PROCESS_GROUP = 0x00000200
    subprocess.Popen([new_exe], cwd=target, close_fds=True,
                     creationflags=DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP)

    # 接管成功的判据：锁文件里的 pid 变成「跑着新版 exe」的那个进程
    want = os.path.normcase(os.path.abspath(new_exe))
    deadline = time.time() + 15
    while time.time() < deadline:
        pid2, exe2 = running_instance()
        if pid2 and os.path.normcase(os.path.abspath(exe2)) == want:
            print(f"[handoff] 新版已接管（pid={pid2}）")
            return True
        time.sleep(0.5)

    print("[handoff] 交接未完成（新版已在运行并给出了提示，或等待超时）。")
    print(f"          可手动退出旧版后启动：{new_exe}")
    return False


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

    # 交接放在版本写回之后：即便交接失败，部署本身已完成、版本号也已推进
    handoff(ver, target)


if __name__ == "__main__":
    main()
