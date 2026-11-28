#!/usr/bin/env python3
"""
ZV-E10 真机运行时状态与安全安装/排错工具（兼容固件 2.02 / 2.03）：
1. --check             检查 USB / Jiritsu 通道、warm.mode 启动状态、进程映射与目标库校验字节（不依赖 od / 本地全分区 backup）
2. --preload-status    查看 dmode、preload、kemco warm.mode、/tmp/zve10plugin.so 与 boot_guard 状态
3. --stage-tmp         通过 Jiritsu (/tmp/jiritsu 中转) 上传 libzve10plugin.so 至 /tmp/libzve10plugin.so 并做 ABI 检查
4. --enable-preload    配置 /setting/mode/dmode=3p 与 preload 指向 /tmp/libzve10plugin.so
5. --enable-coldboot   先完整备份 /setting/kemco.txt 再安全将 warm.mode 切为 0（开启冷启动持久加载，约 1.1s 开机）并回读校验
6. --restore-warmboot  将 /setting/kemco.txt 中的 warm.mode 恢复为原厂 2（暖启动镜像模式）并回读校验
7. --disable-preload   恢复 /setting/mode 为原厂 3 并清空 preload
8. --reset-boot-guard  清除 /tmp/zvp_boot_guard 看门狗计数器
9. --reboot            重启相机（串行命令，避免并发）
10. --logs             拉取 /tmp/zve10plugin.log 诊断日志
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import os
import posixpath
import re
import shlex
import struct
import subprocess
import sys
import tempfile

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(TOOLS_DIR, ".."))
BUILD_SO = os.path.join(REPO_ROOT, "build-arm", "libzve10plugin.so")
PREBUILT_SO = os.path.join(REPO_ROOT, "prebuilt", "libzve10plugin.so")
DEFAULT_SO = BUILD_SO if os.path.isfile(BUILD_SO) else PREBUILT_SO
BACKUP_KEMCO_DIR = os.path.join(REPO_ROOT, "backups", "kemco")

REMOTE_TMP_SO = "/tmp/libzve10plugin.so"
REMOTE_PRELOAD_VAL = "/usr/tool/libmcheckb.so:/tmp/libzve10plugin.so"
REMOTE_KEMCO_PATH = "/setting/kemco.txt"
REMOTE_BOOT_GUARD = "/tmp/zvp_boot_guard"
REMOTE_LOG_PATH = "/tmp/zve10plugin.log"

NETSVC_SO_PATH = "/usr/lib/libNetService.so"
NETSVC_EXPECT_OFFSET = 416440
NETSVC_EXPECT_BYTES = b"\x90\xb5\x95\xb0"

CAUTION_SO_PATH = "/usr/lib/CautionConfig.so"
CAUTION_EXPECT_OFFSET = 14121188
CAUTION_EXPECT_BYTES = b"\x20"

ZVP_BOOT_GUARD_MAGIC = 0x5A564247  # 'ZVBG'

_ACTIVE_CAM_PY: str | None = None


def resolve_cam_py(explicit_path: str | None = None) -> str:
    candidates: list[str] = []
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


def resolve_pmca_root(cam_py: str | None = None) -> str | None:
    candidates: list[str] = []
    env_pmca = os.environ.get("PMCA_ROOT")
    if env_pmca:
        candidates.append(env_pmca)
    resolved_cam = cam_py or resolve_cam_py(_ACTIVE_CAM_PY)
    candidates.extend([
        os.path.join(REPO_ROOT, "third_party", "pmca"),
        os.path.join(REPO_ROOT, "..", "Sony-PMCA-RE"),
        os.path.join(os.path.dirname(resolved_cam), "vendor"),
    ])
    for cand in candidates:
        p = os.path.abspath(cand)
        if os.path.isdir(os.path.join(p, "pmca")):
            return p
    return None


def run_cam(args: list[str], timeout: float = 60.0) -> subprocess.CompletedProcess:
    cam_py = resolve_cam_py(_ACTIVE_CAM_PY)
    if not os.path.isfile(cam_py):
        raise FileNotFoundError(
            f"未找到 camtool cam.py ({cam_py})，请确认已初始化子模块 (git submodule update --init --recursive) 或通过 --cam-py 指定"
        )
    env = os.environ.copy()
    env["MSYS_NO_PATHCONV"] = "1"
    env.setdefault("PYTHONIOENCODING", "utf-8")
    env.setdefault("PYTHONUTF8", "1")
    pmca_root = resolve_pmca_root(cam_py)
    if pmca_root and not env.get("PMCA_ROOT"):
        env["PMCA_ROOT"] = pmca_root
    cmd = [
        "uv", "run",
        "--with", "pyusb",
        "--with", "libusb-package",
        "--with", "pycryptodomex",
        "python", cam_py, *args,
    ]
    return subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
        env=env,
    )


def print_usb_diagnostics(output_text: str) -> None:
    low = output_text.lower()
    print("\n[USB / 驱动排查指引]")
    if "0x0994" in low or "0994" in low or "mtp" in low or "pc" in low:
        print("  - 检测到相机可能处于 MTP / PC遥控 (0x0994) 等非大容量存储模式：请在相机 MENU -> 网络/设置 -> USB连接 中切为 [大容量存储 (Mass Storage)]。")
    print("  - 确认 ZV-E10 已开机、USB 模式为 [大容量存储]，且相机屏幕显示「请检查所连接的设备」时切勿拔线。")
    print("  - Windows 用户请确认以下两个 USB PID 均已在 Zadig 中安装 libusbK 驱动（可勾选 Options -> List All Devices 查看）：")
    print("      1) Sony MSC 大容量存储模式: VID 054C / PID 0D95")
    print("      2) Sony 服务模式 (Senser):   VID 054C / PID 0336  <-- 切入服务模式后常会切到此 PID，若尚未装 libusbK 会报找不到设备或超时")
    print("  - 若之前并发运行过 USB 命令导致通道卡死，请断电/重新插拔相机电池后再重试。")


def parse_kemco_warm_mode(data: bytes) -> tuple[str, int]:
    """严格匹配 b'warm.mode=' 锚点，返回 (当前值字符串, 值字节起始偏移)。"""
    marker = b"warm.mode="
    hits: list[int] = []
    start = 0
    while True:
        idx = data.find(marker, start)
        if idx == -1:
            break
        hits.append(idx)
        start = idx + 1
    if len(hits) != 1:
        raise ValueError(f"kemco.txt 中预期恰好存在 1 处 warm.mode=，实际命中 {len(hits)} 处")

    val_offset = hits[0] + len(marker)
    end = val_offset
    while end < len(data) and data[end : end + 1] not in (b"\r", b"\n", b"\x00"):
        end += 1
    val_raw = data[val_offset:end]
    try:
        val_str = val_raw.decode("ascii").strip()
    except UnicodeDecodeError as exc:
        raise ValueError("warm.mode 值包含非 ASCII 字节") from exc
    if len(val_str) != 1 or val_raw != val_str.encode("ascii"):
        raise ValueError(f"warm.mode 格式异常（预期单字符 0/2），实际={val_raw!r}")
    return val_str, val_offset


def patch_kemco_warm_mode(data: bytes, target_mode: str) -> bytes:
    """安全将 kemco.txt 中的 warm.mode 在 '0' 与 '2' 之间切换，严格保持总字节长度与其余字节不变。"""
    if target_mode not in ("0", "2"):
        raise ValueError(f"不允许的 target warm.mode={target_mode!r}，仅支持 '0' 或 '2'")
    cur_mode, val_offset = parse_kemco_warm_mode(data)
    if cur_mode not in ("0", "2"):
        raise ValueError(f"当前 warm.mode={cur_mode!r} 非受支持的原厂值('0'/'2')，出于安全已中止修改")
    if cur_mode == target_mode:
        return data

    out = bytearray(data)
    out[val_offset] = ord(target_mode)
    patched = bytes(out)

    if len(patched) != len(data):
        raise AssertionError("修改后文件长度发生变化")
    diff_count = sum(1 for a, b in zip(data, patched) if a != b)
    if diff_count != 1:
        raise AssertionError(f"预期仅修改 1 字节，实际差异 {diff_count} 字节")
    verify_mode, _ = parse_kemco_warm_mode(patched)
    if verify_mode != target_mode:
        raise AssertionError("二次解析校验失败")
    return patched


def parse_firmware_probe_slices(raw_bytes: bytes) -> dict[str, object]:
    """解析 5 字节目标库校验切片：前 4 字节 libNetService.so + 第 5 字节 CautionConfig.so。"""
    expected_len = len(NETSVC_EXPECT_BYTES) + len(CAUTION_EXPECT_BYTES)
    if len(raw_bytes) != expected_len:
        raise ValueError(f"固件切片长度异常: 期望 {expected_len} 字节，实际 {len(raw_bytes)} 字节")
    net_bytes = raw_bytes[:4]
    caution_bytes = raw_bytes[4:5]
    return {
        "net_hex": " ".join(f"{b:02x}" for b in net_bytes),
        "net_ok": net_bytes == NETSVC_EXPECT_BYTES,
        "caution_hex": " ".join(f"{b:02x}" for b in caution_bytes),
        "caution_ok": caution_bytes == CAUTION_EXPECT_BYTES,
    }


def extract_cmdline_warm_mode(cmdline_text: str) -> str | None:
    m = re.search(r"\bwarm\.mode=([^\s]+)", cmdline_text)
    return m.group(1) if m else None


def parse_boot_guard_bytes(raw: bytes) -> dict[str, int] | None:
    if len(raw) < 16:
        return None
    magic, version, fail_count, trip_count = struct.unpack("<IIII", raw[:16])
    return {
        "magic": magic,
        "version": version,
        "fail_count": fail_count,
        "trip_count": trip_count,
        "magic_ok": int(magic == ZVP_BOOT_GUARD_MAGIC),
    }


def stage_push_file(local_path: str, remote_path: str, file_mode: str = "0644") -> None:
    """
    兼容 2.02 / 2.03 固件的原子文件推送：
    2.03 固件禁用了 0xff01 FileControl，而 cam.py 仅对 /tmp/jiritsu/ 走 Jiritsu SET_FILE。
    因此统一先推送到 /tmp/jiritsu/ 暂存路径，再在机端 cp 到目标目录同级临时文件并 mv 覆盖。
    """
    if not os.path.isfile(local_path):
        raise FileNotFoundError(f"本地文件不存在: {local_path}")

    if remote_path.startswith("/tmp/jiritsu/"):
        res = run_cam(["put", local_path, remote_path], timeout=120.0)
        if res.returncode != 0:
            raise RuntimeError(f"cam.py put 失败: {res.stdout}\n{res.stderr}".strip())
        return

    base = posixpath.basename(remote_path)
    parent = posixpath.dirname(remote_path) or "/"
    stage_remote = f"/tmp/jiritsu/_zvp_stage_{os.getpid()}_{base}"
    tmp_target = posixpath.join(parent, f".{base}.zvp_tmp_{os.getpid()}")

    res_put = run_cam(["put", local_path, stage_remote], timeout=120.0)
    if res_put.returncode != 0:
        raise RuntimeError(
            f"通过 Jiritsu 通道上传至 {stage_remote} 失败:\n{res_put.stdout}\n{res_put.stderr}".strip()
        )

    expected_size = os.path.getsize(local_path)
    q_stage = shlex.quote(stage_remote)
    q_tmp = shlex.quote(tmp_target)
    q_parent = shlex.quote(parent)
    q_dst = shlex.quote(remote_path)
    install_cmd = (
        f"mkdir -p {q_parent} && "
        f"cp {q_stage} {q_tmp} && "
        f"chmod {file_mode} {q_tmp} && "
        f"mv {q_tmp} {q_dst} && "
        f"sync && "
        f"rm -f {q_stage} && "
        f"ls -l {q_dst}"
    )
    res_mv = run_cam(["sh", install_cmd], timeout=60.0)
    if res_mv.returncode != 0:
        run_cam(["sh", f"rm -f {q_stage} {q_tmp}"], timeout=20.0)
        raise RuntimeError(
            f"机端原子落位至 {remote_path} 失败:\n{res_mv.stdout}\n{res_mv.stderr}".strip()
        )

    lines = [ln.strip() for ln in res_mv.stdout.splitlines() if ln.strip() and not ln.strip().startswith("[exit=")]
    for ln in reversed(lines):
        parts = ln.split()
        if len(parts) >= 5 and parts[4].isdigit():
            actual_size = int(parts[4])
            if actual_size != expected_size:
                raise RuntimeError(f"落位后字节数不匹配: 本地={expected_size}, 机端={actual_size}")
            break


def stage_pull_bytes(remote_path: str) -> bytes:
    """通过 /tmp/jiritsu/ 中转下载远端文件字节（避免 2.03 下 0xff01 FileControl 读失败）。"""
    stage_remote = f"/tmp/jiritsu/_zvp_pull_{os.getpid()}_{posixpath.basename(remote_path)}"
    q_remote = shlex.quote(remote_path)
    q_stage = shlex.quote(stage_remote)

    res_cp = run_cam(["sh", f"test -f {q_remote} && cp {q_remote} {q_stage}"], timeout=40.0)
    if res_cp.returncode != 0:
        raise RuntimeError(f"远端复制 {remote_path} 失败:\n{res_cp.stdout}\n{res_cp.stderr}".strip())

    fd, local_tmp = tempfile.mkstemp(prefix="zvp_pull_", suffix=".bin")
    os.close(fd)
    try:
        res_get = run_cam(["get", stage_remote, local_tmp], timeout=60.0)
        if res_get.returncode != 0:
            raise RuntimeError(f"拉取 {stage_remote} 失败:\n{res_get.stdout}\n{res_get.stderr}".strip())
        with open(local_tmp, "rb") as f:
            return f.read()
    finally:
        run_cam(["sh", f"rm -f {q_stage}"], timeout=20.0)
        try:
            os.remove(local_tmp)
        except OSError:
            pass


def probe_firmware_target_bytes() -> dict[str, object]:
    """只读提取 libNetService.so 与 CautionConfig.so 关键校验字节（零 od 依赖）。"""
    stage_remote = f"/tmp/jiritsu/_zvp_slices_{os.getpid()}.bin"
    q_stage = shlex.quote(stage_remote)
    slice_cmd = (
        f"(dd if={NETSVC_SO_PATH} bs=1 skip={NETSVC_EXPECT_OFFSET} count={len(NETSVC_EXPECT_BYTES)} 2>/dev/null; "
        f"dd if={CAUTION_SO_PATH} bs=1 skip={CAUTION_EXPECT_OFFSET} count={len(CAUTION_EXPECT_BYTES)} 2>/dev/null) "
        f"> {q_stage}"
    )
    res_dd = run_cam(["sh", slice_cmd], timeout=40.0)
    if res_dd.returncode != 0:
        raise RuntimeError(f"采集固件关键字节切片失败:\n{res_dd.stdout}\n{res_dd.stderr}".strip())

    fd, local_tmp = tempfile.mkstemp(prefix="zvp_slices_", suffix=".bin")
    os.close(fd)
    try:
        res_get = run_cam(["get", stage_remote, local_tmp], timeout=40.0)
        if res_get.returncode != 0:
            raise RuntimeError(f"读取切片失败:\n{res_get.stdout}\n{res_get.stderr}".strip())
        with open(local_tmp, "rb") as f:
            raw = f.read()
        return parse_firmware_probe_slices(raw)
    finally:
        run_cam(["sh", f"rm -f {q_stage}"], timeout=20.0)
        try:
            os.remove(local_tmp)
        except OSError:
            pass


def save_kemco_backup(raw_bytes: bytes) -> tuple[str, str]:
    """写入本地不可覆盖原厂基线备份 (kemco.txt.orig) 及当次时间戳备份。"""
    os.makedirs(BACKUP_KEMCO_DIR, exist_ok=True)
    orig_path = os.path.join(BACKUP_KEMCO_DIR, "kemco.txt.orig")
    if not os.path.exists(orig_path):
        with open(orig_path, "wb") as f:
            f.write(raw_bytes)
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    snap_path = os.path.join(BACKUP_KEMCO_DIR, f"kemco.txt.{ts}.bak")
    with open(snap_path, "wb") as f:
        f.write(raw_bytes)
    return orig_path, snap_path


def run_verify_abi(so_path: str) -> int:
    verify_script = os.path.join(TOOLS_DIR, "verify_elf_abi.py")
    if not os.path.isfile(verify_script):
        return 0
    import shutil
    uv_bin = shutil.which("uv")
    cmd = (
        [uv_bin, "run", "--with", "pyelftools", "python", verify_script, so_path]
        if uv_bin
        else [sys.executable, verify_script, so_path]
    )
    proc = subprocess.run(cmd, check=False)
    return proc.returncode


def cmd_check() -> int:
    print("[1/3] 检查相机 USB / Jiritsu / Shell 通道状态...")
    try:
        res_info = run_cam(["info"], timeout=35.0)
        res_sh = run_cam(["sh", "echo ZVP_CAM_OK; uname -a"], timeout=35.0)
    except Exception as exc:
        print(f"[失败] 调用 cam.py 异常: {exc}")
        print_usb_diagnostics(str(exc))
        return 1

    if res_info.returncode == 0 and res_info.stdout.strip():
        print(res_info.stdout.strip())
    if res_sh.returncode != 0 or "ZVP_CAM_OK" not in res_sh.stdout:
        combined = f"{res_info.stdout}\n{res_info.stderr}\n{res_sh.stdout}\n{res_sh.stderr}"
        print(combined.strip())
        print_usb_diagnostics(combined)
        return res_sh.returncode or 1

    print(res_sh.stdout.strip())

    # 对缺失 camtool 全分区 backup 仅作温和提示，不阻塞验证与部署
    cam_dir = os.path.dirname(resolve_cam_py(_ACTIVE_CAM_PY))
    missing_bins = [
        name for name in ("nflasha1.bin", "nflasha2.bin", "nflasha5.bin", "nflasha10.bin")
        if not os.path.isfile(os.path.join(cam_dir, "backup", name))
    ]
    if missing_bins:
        print(f"[提示] 未检测到 camtool 完整分区镜像备份 ({', '.join(missing_bins)})；不影响插件检测与 /setting/kemco.txt 独立备份，但建议有条件时运行 cam.py backup。")

    print("\n[2/3] 读取运行期进程映射、启动参数与关键目录状态 (只读)...")
    probe_sh = (
        "PID=$(pidof im.elf || ps | awk '/im\\.elf/{print $1; exit}'); "
        "echo IM_PID=$PID; "
        "if [ -n \"$PID\" ]; then "
        "  grep -E 'libObj\\.so|libNetService\\.so|CautionConfig\\.so|libbackup\\.so|libInfraFsys\\.so|libzve10plugin\\.so' /proc/$PID/maps || true; "
        "  echo '--- im.elf LD_PRELOAD env ---'; "
        "  tr '\\0' '\\n' < /proc/$PID/environ 2>/dev/null | grep -E '^(LD_PRELOAD|LD_LIBRARY_PATH)=' || echo 'LD_PRELOAD=NONE'; "
        "fi; "
        "echo '--- boot & warmboot state ---'; "
        "echo cmdline=$(cat /proc/cmdline 2>/dev/null || echo NONE); "
        "echo image_dev=$(cat /sys/power/image_dev 2>/dev/null || echo NONE); "
        "echo ssboot_state=$(cat /proc/ssboot/state 2>/dev/null || echo NONE); "
        "echo '--- mode & tmp files ---'; "
        "ls -la /setting/mode/ /tmp/libzve10plugin.so /tmp/zvp_boot_guard /tmp/zve10plugin.log 2>/dev/null || true; "
        "echo dmode=$(cat /setting/mode/dmode 2>/dev/null || echo NONE); "
        "echo preload=$(cat /setting/mode/preload 2>/dev/null || echo NONE); "
        "echo kemco_warm=$(grep -m1 '^warm\\.mode=' /setting/kemco.txt 2>/dev/null || echo NONE); "
        "df -h /tmp /setting /usr 2>/dev/null || true"
    )
    res2 = run_cam(["sh", probe_sh], timeout=45.0)
    print(res2.stdout)
    if res2.returncode != 0 and res2.stderr.strip():
        print(res2.stderr)

    cmdline_mode = None
    kemco_mode = None
    for line in res2.stdout.splitlines():
        if line.startswith("cmdline="):
            cmdline_mode = extract_cmdline_warm_mode(line[len("cmdline="):])
        elif line.startswith("kemco_warm="):
            kemco_mode = line.split("=", 2)[-1].strip() if "warm.mode=" in line else None

    if cmdline_mode == "2" or kemco_mode == "2":
        print("\n[重要提示] 检测到当前相机配置/运行在 warm.mode=2（暖启动镜像模式）！")
        print("           此模式下每次开关机内核会跳过 bootin.elf，导致重启后无法自动加载 /setting/mode/preload。")
        print("           如需开机约 1 秒持久生效插件，可执行: uv run python tools/probe_zve10_runtime.py --enable-coldboot")

    print("\n[3/3] 校验目标固件库关键偏移字节 (通过 Jiritsu 切片，不依赖 od)...")
    try:
        slices = probe_firmware_target_bytes()
    except Exception as exc:
        print(f"[警告] 目标固件库字节校验未能完成: {exc}")
        return 1

    net_tag = "OK" if slices["net_ok"] else "MISMATCH"
    cau_tag = "OK" if slices["caution_ok"] else "MISMATCH"
    print(f"  libNetService.so @ {NETSVC_EXPECT_OFFSET}: [{slices['net_hex']}]  (expect 90 b5 95 b0) -> {net_tag}")
    print(f"  CautionConfig.so @ {CAUTION_EXPECT_OFFSET}: [{slices['caution_hex']}]           (expect 20)          -> {cau_tag}")
    if not (slices["net_ok"] and slices["caution_ok"]):
        print("[警告] 检测到部分字节与 2.01/2.02/2.03 预期不一致，请谨慎操作并反馈机型版本！")
        return 1
    return 0


def cmd_preload_status() -> int:
    print("查询当前 preload、warm.mode 与开机看门狗状态 (只读)...")
    status_sh = (
        "echo dmode=$(cat /setting/mode/dmode 2>/dev/null || echo NONE); "
        "echo preload=$(cat /setting/mode/preload 2>/dev/null || echo NONE); "
        "echo kemco_warm=$(grep -m1 '^warm\\.mode=' /setting/kemco.txt 2>/dev/null || echo NONE); "
        "echo cmdline=$(cat /proc/cmdline 2>/dev/null || echo NONE); "
        "ls -l /tmp/libzve10plugin.so /tmp/zvp_boot_guard /tmp/zve10plugin.log 2>/dev/null || true; "
        "PID=$(pidof im.elf || ps | awk '/im\\.elf/{print $1; exit}'); "
        "echo IM_PID=$PID; "
        "if [ -n \"$PID\" ]; then "
        "  grep 'libzve10plugin\\.so' /proc/$PID/maps || echo 'LOADED_IN_IM=NO'; "
        "fi"
    )
    res = run_cam(["sh", status_sh], timeout=40.0)
    print(res.stdout.strip())
    if res.returncode != 0:
        print(res.stderr)
        return res.returncode

    try:
        bg_raw = stage_pull_bytes(REMOTE_BOOT_GUARD)
        bg = parse_boot_guard_bytes(bg_raw)
        if bg:
            print(
                f"boot_guard: magic=0x{bg['magic']:08x} (ok={bool(bg['magic_ok'])}), "
                f"version={bg['version']}, fail_count={bg['fail_count']}, trip_count={bg['trip_count']}"
            )
    except Exception:
        print("boot_guard: 无文件（健康未触发看门狗或已清理）")
    return 0


def cmd_stage_tmp(so_path: str) -> int:
    if not os.path.exists(so_path):
        print(f"错误: 找不到目标共享库 {so_path}")
        return 1
    abi_rc = run_verify_abi(so_path)
    if abi_rc != 0:
        print("[失败] 本地共享库未通过 ELF/ABI 校验，已终止上传。")
        return abi_rc

    with open(so_path, "rb") as f:
        sha = hashlib.sha256(f.read()).hexdigest()
    print(f"通过 Jiritsu (/tmp/jiritsu 中转) 上传 {so_path} ({os.path.getsize(so_path)} B, sha256={sha[:16]}...) -> {REMOTE_TMP_SO} ...")
    try:
        stage_push_file(so_path, REMOTE_TMP_SO, file_mode="0755")
        run_cam(["sh", f"rm -f {REMOTE_LOG_PATH}"], timeout=20.0)
    except Exception as exc:
        print(f"[失败] 上传失败: {exc}")
        print_usb_diagnostics(str(exc))
        return 1
    print(f"[成功] 已安全写入 {REMOTE_TMP_SO}。")
    return 0


def cmd_logs() -> int:
    res = run_cam(["sh", f"cat {REMOTE_LOG_PATH} 2>/dev/null || echo NO_LOG"], timeout=35.0)
    out = res.stdout.strip()
    print(out)
    if "NO_LOG" in out:
        print("\n[提示] 尚未生成 /tmp/zve10plugin.log，常见原因：")
        print("  1) 相机仍处于 warm.mode=2 暖启动（请运行 --check 查看是否需 --enable-coldboot）")
        print("  2) SD 卡未插入或卡内存在 ZVP_OFF / ZVP_UNINSTALL 文件")
        print("  3) 尚未执行 --enable-preload 并 --reboot")
    return res.returncode


def cmd_enable_preload() -> int:
    print(f"启用原生 preload 引导 (change_mode.sh p {REMOTE_PRELOAD_VAL})...")
    res = run_cam(["sh", f"change_mode.sh p {REMOTE_PRELOAD_VAL}"], timeout=40.0)
    print(res.stdout.strip())
    if res.returncode != 0:
        print(res.stderr)
        return res.returncode

    verify = run_cam(
        ["sh", "echo dmode=$(cat /setting/mode/dmode 2>/dev/null); echo preload=$(cat /setting/mode/preload 2>/dev/null); grep -m1 '^warm\\.mode=' /setting/kemco.txt 2>/dev/null || true"],
        timeout=30.0,
    )
    print(verify.stdout.strip())
    if "warm.mode=2" in verify.stdout:
        print("\n[注意] 已启用 /setting/mode/preload，但当前 /setting/kemco.txt 为 warm.mode=2。")
        print("       零售机 2.02/2.03 暖启动会跳过 bootin.elf；如需重启持久生效请执行:")
        print("       uv run python tools/probe_zve10_runtime.py --enable-coldboot")
    else:
        print("下一步请执行 --reboot 重启相机使插件生效。")
    return 0


def cmd_disable_preload() -> int:
    print("恢复原厂默认启动模式 (change_mode.sh) 并清空 preload...")
    res = run_cam(
        ["sh", "change_mode.sh; echo -n > /setting/mode/preload; sync; echo dmode=$(cat /setting/mode/dmode 2>/dev/null); echo preload=$(cat /setting/mode/preload 2>/dev/null)"],
        timeout=40.0,
    )
    print(res.stdout.strip())
    if res.returncode != 0:
        print(res.stderr)
    else:
        print("已取消 preload 注入，--reboot 后恢复纯原厂行为。")
    return res.returncode


def _switch_kemco_warm_mode(target_mode: str) -> int:
    action_desc = "冷启动持久模式 (warm.mode=0)" if target_mode == "0" else "原厂暖启动模式 (warm.mode=2)"
    print(f"正在读取机端 {REMOTE_KEMCO_PATH} 准备切换至 {action_desc}...")
    try:
        orig_bytes = stage_pull_bytes(REMOTE_KEMCO_PATH)
        cur_mode, _ = parse_kemco_warm_mode(orig_bytes)
    except Exception as exc:
        print(f"[失败] 读取或解析 {REMOTE_KEMCO_PATH} 失败: {exc}")
        return 1

    orig_sha = hashlib.sha256(orig_bytes).hexdigest()
    orig_bak, snap_bak = save_kemco_backup(orig_bytes)
    print(f"已备份原始 kemco.txt ({len(orig_bytes)} B, sha256={orig_sha[:16]}...):")
    print(f"  - 基线备份: {orig_bak}")
    print(f"  - 本次快照: {snap_bak}")

    if cur_mode == target_mode:
        print(f"[提示] 当前 {REMOTE_KEMCO_PATH} 已是 warm.mode={target_mode}，无需修改。")
        return 0

    try:
        patched_bytes = patch_kemco_warm_mode(orig_bytes, target_mode)
    except Exception as exc:
        print(f"[失败] 构造修改字节失败，已中止写入: {exc}")
        return 1

    expected_sha = hashlib.sha256(patched_bytes).hexdigest()
    fd, local_tmp = tempfile.mkstemp(prefix="zvp_kemco_", suffix=".txt")
    os.close(fd)
    try:
        with open(local_tmp, "wb") as f:
            f.write(patched_bytes)
        print(f"正在精确修改 1 字节 (warm.mode={cur_mode} -> {target_mode}, 新 sha256={expected_sha[:16]}...) 并回传...")
        stage_push_file(local_tmp, REMOTE_KEMCO_PATH, file_mode="0644")

        readback = stage_pull_bytes(REMOTE_KEMCO_PATH)
        readback_sha = hashlib.sha256(readback).hexdigest()
        rb_mode, _ = parse_kemco_warm_mode(readback)
        if readback_sha != expected_sha or rb_mode != target_mode:
            print(f"[严重告警] 回读校验不一致! 期望 sha256={expected_sha}，实际={readback_sha}")
            print("正在尝试自动回滚到本次修改前备份...")
            with open(local_tmp, "wb") as fb:
                fb.write(orig_bytes)
            stage_push_file(local_tmp, REMOTE_KEMCO_PATH, file_mode="0644")
            return 2

        print(f"[成功] {REMOTE_KEMCO_PATH} 已安全切换为 warm.mode={target_mode} 且 SHA-256 回读一致！")
        print("下一步请执行 --reboot 使内核与启动流程按新模式启动。")
        return 0
    except Exception as exc:
        print(f"[失败] 写入或校验 {REMOTE_KEMCO_PATH} 失败: {exc}")
        return 1
    finally:
        try:
            os.remove(local_tmp)
        except OSError:
            pass


def cmd_reset_boot_guard() -> int:
    print(f"正在清理 {REMOTE_BOOT_GUARD}...")
    res = run_cam(["sh", f"rm -f {REMOTE_BOOT_GUARD} && ls -l {REMOTE_BOOT_GUARD} 2>/dev/null || echo BOOT_GUARD_CLEARED"], timeout=30.0)
    print(res.stdout.strip())
    return res.returncode


def cmd_reboot(wait_sec: int = 30) -> int:
    print("[1/2] 发送 reboot reboot --confirm 重启相机...")
    res = run_cam(["reboot", "reboot", "--confirm"], timeout=90.0)
    print(res.stdout.strip())
    if res.returncode != 0:
        if res.stderr.strip():
            print(res.stderr)
        return res.returncode
    print(f"[2/2] 等待相机重启重新连线 (最多 {wait_sec}s)...")
    res2 = run_cam(["wait", str(wait_sec)], timeout=float(wait_sec + 30))
    print(res2.stdout.strip())
    if res2.returncode != 0 and res2.stderr.strip():
        print(res2.stderr)
    return res2.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="ZV-E10 真机运行时探测与安装工具 (2.02 / 2.03)")
    parser.add_argument("--so", default=DEFAULT_SO, help=f"libzve10plugin.so 路径 (默认: {DEFAULT_SO})")
    parser.add_argument("--cam-py", default=None, help="显式指定 camtool cam.py 路径 (默认自动查找)")
    parser.add_argument("--check", action="store_true", help="检查 USB 通道、warm.mode 与固件校验字节")
    parser.add_argument("--preload-status", action="store_true", help="查看 dmode/preload/kemco/boot_guard 状态")
    parser.add_argument("--stage-tmp", action="store_true", help="上传 .so 到 /tmp/libzve10plugin.so (兼容 2.03)")
    parser.add_argument("--enable-preload", action="store_true", help="启用 preload 注入 (/setting/mode/dmode=3p)")
    parser.add_argument("--disable-preload", action="store_true", help="恢复默认模式 (/setting/mode/dmode=3)")
    parser.add_argument("--enable-coldboot", action="store_true", help="备份并切换 kemco.txt warm.mode=0 以开启冷启动持久加载")
    parser.add_argument("--restore-warmboot", action="store_true", help="还原 kemco.txt warm.mode=2 恢复原厂暖启动")
    parser.add_argument("--reset-boot-guard", action="store_true", help="清理 /tmp/zvp_boot_guard 看门狗计数")
    parser.add_argument("--reboot", action="store_true", help="重启相机")
    parser.add_argument("--logs", action="store_true", help="读取 /tmp/zve10plugin.log")
    args = parser.parse_args()

    global _ACTIVE_CAM_PY
    _ACTIVE_CAM_PY = args.cam_py

    actions = [
        args.check,
        args.preload_status,
        args.stage_tmp,
        args.enable_preload,
        args.disable_preload,
        args.enable_coldboot,
        args.restore_warmboot,
        args.reset_boot_guard,
        args.reboot,
        args.logs,
    ]
    if not any(actions):
        parser.print_help()
        return 0

    if args.check:
        rc = cmd_check()
        if rc != 0:
            return rc
    if args.preload_status:
        rc = cmd_preload_status()
        if rc != 0:
            return rc
    if args.reset_boot_guard:
        rc = cmd_reset_boot_guard()
        if rc != 0:
            return rc
    if args.stage_tmp:
        rc = cmd_stage_tmp(args.so)
        if rc != 0:
            return rc
    if args.enable_preload:
        rc = cmd_enable_preload()
        if rc != 0:
            return rc
    if args.enable_coldboot:
        rc = _switch_kemco_warm_mode("0")
        if rc != 0:
            return rc
    if args.restore_warmboot:
        rc = _switch_kemco_warm_mode("2")
        if rc != 0:
            return rc
    if args.disable_preload:
        rc = cmd_disable_preload()
        if rc != 0:
            return rc
    if args.reboot:
        rc = cmd_reboot()
        if rc != 0:
            return rc
    if args.logs:
        rc = cmd_logs()
        if rc != 0:
            return rc
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
