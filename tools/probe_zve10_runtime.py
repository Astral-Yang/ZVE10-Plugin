#!/usr/bin/env python3
"""
Sony ZV-E10 运行期探针与 Phase 1 部署验证工具 (基于 camtool)
功能：
  --check       只读检查相机连接、关键库加载基址、5GHz 补丁点原字节校验与 Backup 读取
  --stage-tmp   将编译好的 libzve10plugin.so 推送到 /tmp/libzve10plugin.so（不改 /setting）
  --logs        拉取相机端 /tmp/zve10plugin.log 诊断日志
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(TOOLS_DIR, ".."))
BUILD_SO = os.path.join(REPO_ROOT, "build-arm", "libzve10plugin.so")
PREBUILT_SO = os.path.join(REPO_ROOT, "prebuilt", "libzve10plugin.so")
DEFAULT_SO = BUILD_SO if os.path.isfile(BUILD_SO) else PREBUILT_SO


def resolve_cam_py(explicit_path: str | None = None) -> str:
    candidates = []
    if explicit_path:
        candidates.append(explicit_path)
    env_cam = os.environ.get("ZVP_CAM_PY")
    if env_cam:
        candidates.append(env_cam)
    candidates.extend([
        os.path.join(REPO_ROOT, "third_party", "camtool", "cam.py"),
        os.path.join(TOOLS_DIR, "camtool", "cam.py"),
        os.path.join(REPO_ROOT, "camtool", "camtool", "cam.py"),
        os.path.join(REPO_ROOT, "..", "camtool", "camtool", "cam.py"),
        os.path.join(REPO_ROOT, "..", "zve10", "camtool", "camtool", "cam.py"),
        os.path.join(REPO_ROOT, "..", "..", "camtool", "camtool", "cam.py"),
    ])
    for cand in candidates:
        p = os.path.abspath(cand)
        if os.path.isfile(p):
            return p
    return os.path.abspath(candidates[0])


CAM_PY = resolve_cam_py()


def resolve_pmca_root() -> str | None:
    candidates = []
    env_pmca = os.environ.get("PMCA_ROOT")
    if env_pmca:
        candidates.append(env_pmca)
    candidates.extend([
        os.path.join(REPO_ROOT, "third_party", "pmca"),
        os.path.join(REPO_ROOT, "..", "Sony-PMCA-RE"),
        os.path.join(os.path.dirname(CAM_PY), "vendor"),
    ])
    for cand in candidates:
        p = os.path.abspath(cand)
        if os.path.isdir(os.path.join(p, "pmca")):
            return p
    return None


def run_cam(args: list[str], capture: bool = True) -> subprocess.CompletedProcess:
    if not os.path.isfile(CAM_PY):
        raise FileNotFoundError(
            f"未找到 cam.py: {CAM_PY}。请运行 `git submodule update --init --recursive` 拉取 third_party/camtool，或通过 --cam-py / ZVP_CAM_PY 指定路径"
        )
    env = os.environ.copy()
    env["MSYS_NO_PATHCONV"] = "1"
    env["PYTHONIOENCODING"] = "utf-8"
    pmca_root = resolve_pmca_root()
    if pmca_root and not env.get("PMCA_ROOT"):
        env["PMCA_ROOT"] = pmca_root
    cmd = ["uv", "run", "--with", "pyusb", "--with", "libusb-package", "--with", "pycryptodomex", "python", CAM_PY] + args
    return subprocess.run(
        cmd,
        env=env,
        capture_output=capture,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def cmd_check() -> int:
    print("[1/3] 检查相机 USB/ADB-over-MSC 连接状态 (cam.py doctor)...")
    res = run_cam(["doctor"])
    print(res.stdout)
    if res.returncode != 0:
        print(res.stderr)
        print("[提示] 请确认 ZV-E10 已开机、USB 模式设为大容量存储并连线至电脑。")
        return res.returncode

    print("[2/3] 读取运行期进程映射与关键目录状态 (只读)...")
    probe_sh = (
        "PID=$(pidof im.elf || ps | awk '/im\\.elf/{print $1; exit}'); "
        "echo IM_PID=$PID; "
        "if [ -n \"$PID\" ]; then "
        "  grep -E 'libObj\\.so|libNetService\\.so|CautionConfig\\.so|libbackup\\.so|libInfraFsys\\.so' /proc/$PID/maps; "
        "fi; "
        "echo '--- mode files ---'; "
        "ls -la /setting/mode/ 2>/dev/null || true; "
        "echo dmode=$(cat /setting/mode/dmode 2>/dev/null || echo NONE); "
        "echo preload=$(cat /setting/mode/preload 2>/dev/null || echo NONE); "
        "echo '--- verify firmware bytes on disk ---'; "
        "dd if=/usr/lib/libNetService.so bs=1 skip=416440 count=4 2>/dev/null | od -tx1 -An; echo '  (expect 90 b5 95 b0)'; "
        "dd if=/usr/lib/CautionConfig.so bs=1 skip=14121188 count=1 2>/dev/null | od -tx1 -An; echo '  (expect 20)'; "
        "df -h /tmp /setting /usr 2>/dev/null || true"
    )
    res2 = run_cam(["sh", probe_sh])
    print(res2.stdout)
    if res2.stderr:
        print(res2.stderr)
    return res2.returncode


def cmd_stage_tmp(so_path: str) -> int:
    if not os.path.exists(so_path):
        print(f"[错误] 找不到共享库文件: {so_path}")
        return 1
    print("[1/3] 解除旧目标文件链接（避免覆写已映射只读页）...")
    _ = run_cam(["sh", "rm -f /tmp/libzve10plugin.so /tmp/zve10plugin.log"])
    print(f"[2/3] 推送 {so_path} -> /tmp/libzve10plugin.so ...")
    res = run_cam(["put", so_path, "/tmp/libzve10plugin.so"])
    print(res.stdout)
    if res.returncode != 0:
        print(res.stderr)
        return res.returncode

    print("[3/3] 验证文件权限与大小...")
    res2 = run_cam(["sh", "chmod 755 /tmp/libzve10plugin.so && ls -lh /tmp/libzve10plugin.so"])
    print(res2.stdout)
    return res2.returncode


def cmd_logs() -> int:
    res = run_cam(["sh", "cat /tmp/zve10plugin.log 2>/dev/null || echo NO_LOG"])
    print(res.stdout)
    return res.returncode


def cmd_enable_preload() -> int:
    preload_val = "/usr/tool/libmcheckb.so:/tmp/libzve10plugin.so"
    print(f"启用原生 preload 引导 (change_mode.sh p {preload_val})...")
    res = run_cam(["sh", f"change_mode.sh p {preload_val}"])
    print(res.stdout)
    return res.returncode


def cmd_disable_preload() -> int:
    print("恢复原厂默认启动模式 (change_mode.sh 3)...")
    res = run_cam(["sh", "change_mode.sh 3"])
    print(res.stdout)
    return res.returncode


def cmd_reboot(wait_sec: int = 30) -> int:
    print("[1/2] 重启相机 (cam.py reboot reboot --confirm)...")
    res = run_cam(["reboot", "reboot", "--confirm"])
    print(res.stdout)
    if res.returncode != 0:
        if res.stderr:
            print(res.stderr)
        return res.returncode
    print(f"[2/2] 等待相机重启重新连线 (最多 {wait_sec}s)...")
    res2 = run_cam(["wait", str(wait_sec)])
    print(res2.stdout)
    if res2.stderr:
        print(res2.stderr)
    return res2.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="Sony ZV-E10 运行期只读探针与部署助手")
    parser.add_argument("--check", action="store_true", help="只读检查相机连接与 im.elf 映射")
    parser.add_argument("--stage-tmp", action="store_true", help="上传 libzve10plugin.so 到相机 /tmp")
    parser.add_argument("--logs", action="store_true", help="读取 /tmp/zve10plugin.log")
    parser.add_argument("--enable-preload", action="store_true", help="设置 /setting/mode/preload (重启后生效)")
    parser.add_argument("--disable-preload", action="store_true", help="恢复默认 mode3 无注入")
    parser.add_argument("--reboot", action="store_true", help="重启相机并等待 30 秒重新连线")
    parser.add_argument("--so", default=DEFAULT_SO, help="libzve10plugin.so 路径")
    parser.add_argument("--cam-py", default=None, help="指定 camtool cam.py 路径（也可设 ZVP_CAM_PY）")
    args = parser.parse_args()

    global CAM_PY
    if args.cam_py:
        CAM_PY = resolve_cam_py(args.cam_py)

    if args.check:
        return cmd_check()
    if args.stage_tmp:
        return cmd_stage_tmp(args.so)
    if args.logs:
        return cmd_logs()
    if args.enable_preload:
        return cmd_enable_preload()
    if args.disable_preload:
        return cmd_disable_preload()
    if args.reboot:
        return cmd_reboot(30)

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
