#!/usr/bin/env python3
"""
Sony ZV-E10 运行期探针与 Phase 1 部署验证工具 (基于 camtool)
功能（兼容固件 2.01 / 2.02 / 2.03）：
  --check           检查 USB / Jiritsu / shell 通道、warm.mode、进程映射与固件基线字节（不依赖 od）
  --stage-tmp       上传 libzve10plugin.so 至 /tmp/libzve10plugin.so（先 ABI 校验，经 /tmp/jiritsu 原子落位）
  --logs            拉取相机端 /tmp/zve10plugin.log 诊断日志
  --preload-status  只读核对 preload 配置、kemco warm.mode、开机日志指纹、im.elf 映射与看门狗状态
  --enable-preload  写入合规的 dmode=3p + preload 内容（见 tools/zvp_preload_gate.py）
  --disable-preload 恢复原厂 mode 3 并清空 preload
  --enable-coldboot 备份 /setting/kemco.txt 后把 warm.mode 切为 0（冷启动持久加载）
  --restore-warmboot 把 /setting/kemco.txt 的 warm.mode 还原原厂 2（暖启动镜像模式）
  --reset-boot-guard 清理 /tmp/zvp_boot_guard 看门狗计数
  --backend jiritsu 走 2.03 加密 Jiritsu 通道（配 --deploy / --wbi-* 使用）

关于 preload 内容：bootin.elf 只接受以 4 条 /usr/tool/* 路径为前缀的内容，
详见 tools/zvp_preload_gate.py。

合并说明（2026-11-28，上游 e84d9a7）：本文件在保留分支 Jiritsu/WBI 流程的前提下，
吸收上游新增的 2.02/2.03 兼容能力：/tmp/jiritsu 原子暂存、无 od 依赖的固件字节校验、
kemco warm.mode 冷启动开关（首次原厂备份 + 写后 SHA-256 回读自动回滚）与 ABI 前置校验。
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
from pathlib import Path

import zvp_preload_gate as preload_gate

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(TOOLS_DIR, ".."))
BUILD_SO = os.path.join(REPO_ROOT, "build-arm", "libzve10plugin.so")
PREBUILT_SO = os.path.join(REPO_ROOT, "prebuilt", "libzve10plugin.so")
DEFAULT_SO = BUILD_SO if os.path.isfile(BUILD_SO) else PREBUILT_SO
# 相机上插件常驻路径；也是 preload 内容的第二段（第一段见 zvp_preload_gate.GATE_DECOY）
PLUGIN_PRELOAD_PATH = "/tmp/libzve10plugin.so"
REMOTE_TMP_SO = PLUGIN_PRELOAD_PATH
# 2.03 原厂 /usr/bin/im.elf 的哈希，用于确认真机跑的就是分析样本
IM_ELF_SHA256_FW203 = "3fd399b62b468c0302b370362378a3719df6b7d68ff9f6e1c09312a84beeabbf"

REMOTE_KEMCO_PATH = "/setting/kemco.txt"
REMOTE_BOOT_GUARD = "/tmp/zvp_boot_guard"
REMOTE_LOG_PATH = "/tmp/zve10plugin.log"
BACKUP_KEMCO_DIR = os.path.join(REPO_ROOT, "backups", "kemco")

# 固件关键校验字节（libNetService.so / CautionConfig.so 的原厂偏移）
NETSVC_SO_PATH = "/usr/lib/libNetService.so"
NETSVC_EXPECT_OFFSET = 416440
NETSVC_EXPECT_BYTES = b"\x90\xb5\x95\xb0"
CAUTION_SO_PATH = "/usr/lib/CautionConfig.so"
CAUTION_EXPECT_OFFSET = 14121188
CAUTION_EXPECT_BYTES = b"\x20"
ZVP_BOOT_GUARD_MAGIC = 0x5A564247  # 'ZVBG'

# 当前 USB 传输层。Jiritsu 后端（zve10_jiritsu_runtime）会把 run_cam 换成加密通道，
# 并把它置为 "jiritsu"：该通道只支持 put（插件直写），因此不能走 /tmp/jiritsu 原子暂存。
TRANSPORT = "camtool"


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


def run_cam(args: list[str], capture: bool = True,
            timeout: float | None = None) -> subprocess.CompletedProcess:
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
        timeout=timeout,
    )


def print_usb_diagnostics(output_text: str) -> None:
    """USB 通道失败时给出具体排查方向（上游 e84d9a7 引入）。"""
    low = output_text.lower()
    print("\n[USB / 驱动排查指引]")
    if "0x0994" in low or "0994" in low or "mtp" in low:
        print("  - 检测到相机可能处于 MTP / PC遥控 (0x0994) 等非大容量存储模式：请在相机 MENU -> 网络/设置 -> USB连接 中切为 [大容量存储 (Mass Storage)]。")
    print("  - 确认 ZV-E10 已开机、USB 模式为 [大容量存储]，且相机屏幕显示「请检查所连接的设备」时切勿拔线。")
    print("  - Windows 用户请确认以下两个 USB PID 均已在 Zadig 中安装 libusbK 驱动（可勾选 Options -> List All Devices 查看）：")
    print("      1) Sony MSC 大容量存储模式: VID 054C / PID 0D95")
    print("      2) Sony 服务模式 (Senser):   VID 054C / PID 0336  <-- 切入服务模式后常会切到此 PID，若尚未装 libusbK 会报找不到设备或超时")
    print("  - 若之前并发运行过 USB 命令导致通道卡死，请断电/重新插拔相机电池后再重试。")


def _normalize_hex(raw: str) -> str:
    """把 xxd -p（连续十六进制）或 od -tx1（空格分隔）统一成 '90 b5 95 b0' 形式。"""
    digits = re.sub(r"[^0-9a-fA-F]", "", raw or "")
    if not digits or len(digits) % 2:
        return ""
    return " ".join(digits[i:i + 2].lower() for i in range(0, len(digits), 2))


def _warm_mode_hint(probe_output: str) -> str | None:
    """从 --check 的只读输出判定相机是否运行在 warm.mode=2（暖启动镜像）。"""
    cmdline_mode = None
    kemco_mode = None
    for line in probe_output.splitlines():
        if line.startswith("cmdline="):
            cmdline_mode = extract_cmdline_warm_mode(line[len("cmdline="):])
        elif line.startswith("kemco_warm="):
            match = re.search(r"warm\.mode=([^\s]+)", line)
            kemco_mode = match.group(1) if match else None
    if cmdline_mode != "2" and kemco_mode != "2":
        return None
    return (
        "\n[重要提示] 检测到当前相机配置/运行在 warm.mode=2（暖启动镜像模式）！\n"
        "           此模式下开关机内核会跳过 bootin.elf，重启后不会自动加载 /setting/mode/preload。\n"
        "           如需开机约 1 秒即持久生效，可执行: uv run python tools/probe_zve10_runtime.py --enable-coldboot"
    )


def read_firmware_bytes() -> subprocess.CompletedProcess:
    """只读采集两个目标库的关键字节，不依赖相机 PATH 里的 od/tr。

    优先 `busybox dd | busybox xxd -p` 输出连续十六进制；busybox 没有 xxd 时退回
    od 输出。两条路径都由主机侧 `_normalize_hex` 归一化，任一不可用则输出空值，
    由调用方按 MISSING 处理（不静默通过）。
    """
    shell = (
        f"NET=$(busybox dd if={NETSVC_SO_PATH} bs=1 skip={NETSVC_EXPECT_OFFSET} "
        f"count={len(NETSVC_EXPECT_BYTES)} 2>/dev/null | busybox xxd -p 2>/dev/null); "
        f"[ -n \"$NET\" ] || NET=$(busybox dd if={NETSVC_SO_PATH} bs=1 skip={NETSVC_EXPECT_OFFSET} "
        f"count={len(NETSVC_EXPECT_BYTES)} 2>/dev/null | busybox od -An -tx1 2>/dev/null); "
        f"CAU=$(busybox dd if={CAUTION_SO_PATH} bs=1 skip={CAUTION_EXPECT_OFFSET} "
        f"count={len(CAUTION_EXPECT_BYTES)} 2>/dev/null | busybox xxd -p 2>/dev/null); "
        f"[ -n \"$CAU\" ] || CAU=$(busybox dd if={CAUTION_SO_PATH} bs=1 skip={CAUTION_EXPECT_OFFSET} "
        f"count={len(CAUTION_EXPECT_BYTES)} 2>/dev/null | busybox od -An -tx1 2>/dev/null); "
        "echo NETSERVICE_BYTES=$NET; "
        "echo CAUTION_BYTES=$CAU"
    )
    return run_cam(["sh", shell], timeout=45.0)


def cmd_check() -> int:
    print("[1/3] 检查相机 USB / Jiritsu / shell 通道状态...")
    res = run_cam(["doctor"], timeout=35.0)
    print(res.stdout)
    if res.returncode != 0:
        print(res.stderr)
        print_usb_diagnostics(f"{res.stdout}\n{res.stderr}")
        print("[提示] 请确认 ZV-E10 已开机、USB 模式设为大容量存储并连线至电脑。")
        return res.returncode

    # 缺失 camtool 全分区备份只做温和提示：不影响插件检测与 kemco.txt 独立备份
    if TRANSPORT == "camtool":
        cam_dir = os.path.dirname(CAM_PY)
        missing = [name for name in ("nflasha1.bin", "nflasha2.bin", "nflasha5.bin", "nflasha10.bin")
                   if not os.path.isfile(os.path.join(cam_dir, "backup", name))]
        if missing:
            print(f"[提示] 未检测到 camtool 完整分区镜像备份 ({', '.join(missing)})；"
                  "不影响插件检测与 /setting/kemco.txt 独立备份，有条件时再运行 cam.py backup。")

    print("[2/3] 读取运行期进程映射、启动参数与关键目录状态 (只读)...")
    probe_sh = (
        "PID=$(pidof im.elf || ps | awk '/im\\.elf/{print $1; exit}'); "
        "echo IM_PID=$PID; "
        'if [ -n "$PID" ]; then '
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

    hint = _warm_mode_hint(res2.stdout)
    if hint:
        print(hint)

    print("[3/3] 校验目标固件库关键偏移字节（不依赖 od）...")
    baseline = read_firmware_bytes()
    print(baseline.stdout)
    if baseline.returncode != 0:
        print(baseline.stderr)
        return baseline.returncode
    values: dict[str, str] = {}
    for name in ("NETSERVICE_BYTES", "CAUTION_BYTES"):
        match = re.search(rf"^{name}=(.*)$", baseline.stdout, re.MULTILINE)
        values[name] = _normalize_hex(match.group(1) if match else "")
    for name, expected in (("NETSERVICE_BYTES", "90 b5 95 b0"), ("CAUTION_BYTES", "20")):
        actual = values.get(name) or "MISSING"
        tag = "OK" if actual == expected else "MISMATCH"
        print(f"  {name}: [{actual}] (expect {expected}) -> {tag}")
        if actual != expected:
            print(f"[错误] {name}={actual}，期望 {expected}，停止部署")
            return 1
    return 0


def cmd_stage_tmp(so_path: str) -> int:
    if not os.path.exists(so_path):
        print(f"[错误] 找不到共享库文件: {so_path}")
        return 1

    print("[1/4] 校验 ELF/ABI 约束（ARMv7 soft-float，GLIBC <= 2.11.2）...")
    if TRANSPORT == "jiritsu":
        # Jiritsu 会话在连接阶段已由 Runtime.verify_plugin() 做过同一套 ABI 校验
        print("      Jiritsu 会话内已校验，跳过重复检查")
        abi_rc = 0
    else:
        abi_rc = run_verify_abi(so_path)
    if abi_rc != 0:
        print("[失败] 共享库未通过 ELF/ABI 校验，已终止上传。")
        return abi_rc

    print("[2/4] 解除旧目标文件链接并清理上次失败的临时文件...")
    removed = run_cam(["sh",
                       "rm -f /tmp/libzve10plugin.so /tmp/libzve10plugin.so.* /tmp/zve10plugin.log "
                       "/tmp/jiritsu/_zvp_stage_*; "
                       ": > /tmp/kikilog.dat 2>/dev/null || true; "
                       "echo '--- /tmp after cleanup ---'; busybox df -h /tmp 2>/dev/null || true; "
                       "ls -lah /tmp 2>/dev/null | busybox tail -n 30"], timeout=60.0)
    print(removed.stdout)
    if removed.returncode != 0:
        print(removed.stderr)
        return removed.returncode

    print(f"[3/4] 上传 {so_path} -> {PLUGIN_PRELOAD_PATH} ...")
    try:
        if TRANSPORT == "jiritsu":
            # 加密通道只支持把插件直写 /tmp；Runtime.put 内部已做分块、SHA-256 回读与 mv
            res = run_cam(["put", so_path, PLUGIN_PRELOAD_PATH], timeout=180.0)
            print(res.stdout)
            if res.returncode != 0:
                print(res.stderr)
                return res.returncode
        else:
            # camtool：经 /tmp/jiritsu 暂存 + 目标目录临时文件原子 mv，兼容 2.03（0xff01 被禁）
            stage_push_file(so_path, PLUGIN_PRELOAD_PATH, file_mode="0755")
            print(f"      机端原子落位完成 -> {PLUGIN_PRELOAD_PATH}")
    except Exception as exc:
        print(f"[失败] 上传失败: {exc}")
        print_usb_diagnostics(str(exc))
        return 1

    print("[4/4] 验证文件权限与大小...")
    res2 = run_cam(["sh", f"chmod 755 {PLUGIN_PRELOAD_PATH} && ls -lh {PLUGIN_PRELOAD_PATH}"],
                   timeout=30.0)
    print(res2.stdout)
    if res2.returncode != 0 and res2.stderr.strip():
        print(res2.stderr)
    return res2.returncode


def cmd_logs() -> int:
    res = run_cam(["sh", f"cat {REMOTE_LOG_PATH} 2>/dev/null || echo NO_LOG"], timeout=35.0)
    out = res.stdout.strip()
    print(out)
    if "NO_LOG" in out:
        print("\n[提示] 尚未生成 /tmp/zve10plugin.log，常见原因：")
        print("  1) 相机仍处于 warm.mode=2 暖启动（先跑 --check 看是否需 --enable-coldboot）")
        print("  2) SD 卡未插入，或卡内有 ZVP_OFF / ZVP_UNINSTALL（插件门禁自熔断）")
        print("  3) 尚未执行 --enable-preload 并重启")
    return res.returncode


def cmd_enable_preload() -> int:
    """写入 bootin.elf 能接受的 dmode/preload 组合。

    关键点（逆向结论）：
      * `p` 置位 bootin 的 preload 位，`3` 选择 app 阶段 —— 两者都要写进 dmode；
      * preload 文件内容必须以 4 条 /usr/tool/* 之一为前缀，否则 bootin 静默拒绝；
      * 因此首段放一个"前缀命中但保证不存在"的占位项，第二段才是插件路径。
        设备加载器会打印一则 `cannot be preloaded: ignored` 并继续，程序正常启动。
    """
    print("[1/4] 离线复现 bootin.elf 的 preload 门禁...")
    print(f"      基线 bootin.elf sha256 = {preload_gate.BOOTIN_ELF_SHA256_FW203}")
    try:
        content = preload_gate.build_preload_value(PLUGIN_PRELOAD_PATH)
    except ValueError as exc:
        print(f"[错误] 无法构造合规的 preload 内容: {exc}")
        return 1
    gate = preload_gate.simulate(content.encode("ascii"))
    print(f"      写入内容 = {content}")
    print(f"      门禁     = 通过（规则 #{gate.rule}: {gate.matched_prefix}）")
    print(f"      LD_PRELOAD => {gate.ld_preload}")

    print("[2/4] 写入 dmode=3p 与 preload（change_mode.sh p）...")
    command = (
        f'PRELOAD="{content}"; '
        'RC=0; change_mode.sh p "$PRELOAD" || RC=1; '
        'D=$(cat /setting/mode/dmode 2>/dev/null); '
        'P=$(cat /setting/mode/preload 2>/dev/null); '
        'K=$(grep -m1 "^warm\\.mode=" /setting/kemco.txt 2>/dev/null || echo NONE); '
        'echo "dmode=$D"; echo "preload=$P"; echo "kemco=$K"; '
        '[ "$RC" = "0" ] || echo "[ERR] change_mode.sh failed"; '
        f'[ "$D" = "{preload_gate.DMODE_PRELOAD_VALUE}" ] || '
        f'echo "[ERR] dmode mismatch (expect {preload_gate.DMODE_PRELOAD_VALUE})"; '
        '[ "$P" = "$PRELOAD" ] || echo "[ERR] preload readback mismatch"; '
        f'[ "$RC" = "0" ] && [ "$D" = "{preload_gate.DMODE_PRELOAD_VALUE}" ] '
        '&& [ "$P" = "$PRELOAD" ] && echo "[3/4] readback OK"'
    )
    res = run_cam(["sh", command], timeout=40.0)
    print(res.stdout)
    if res.returncode != 0:
        if res.stderr:
            print(res.stderr)
        return res.returncode

    if "warm.mode=2" in res.stdout:
        print("\n[注意] 已写入 /setting/mode/preload，但 /setting/kemco.txt 当前为 warm.mode=2。")
        print("       2.02/2.03 暖启动会跳过 bootin.elf；如需重启后持久生效请执行:")
        print("       uv run python tools/probe_zve10_runtime.py --enable-coldboot")
    else:
        print("\n[4/4] 下一步执行 --reboot 重启相机使插件生效。")
    print("      重启后的判读依据（用 --preload-status 只读核对）:")
    print(f"      ulogio 应出现 : LD_PRELOAD={content}")
    print(f"      加载器警告    : object '{preload_gate.GATE_DECOY}' from LD_PRELOAD cannot be preloaded: ignored.")
    print("      im.elf maps   : /tmp/libzve10plugin.so")
    return 0


def _hex_block(text: str, name: str) -> bytes | None:
    """解析 `xxd -p` 输出块；块内容被清洗为纯十六进制后再还原字节。"""
    match = re.search(rf"{re.escape(name)}_BEGIN\r?\n(.*?){re.escape(name)}_END",
                      text, re.DOTALL)
    if not match:
        return None
    payload = re.sub(r"[^0-9a-fA-F]", "", match.group(1))
    if not payload or len(payload) % 2:
        return None
    return bytes.fromhex(payload)


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
    """兼容 2.02 / 2.03 固件的原子文件推送。

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
    """推送前强制 ELF/ABI 校验（AGENTS.md 红线；脚本缺失时放行以免阻塞其他诊断）。"""
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


def cmd_preload_status() -> int:
    """只读采集：配置、白名单库、开机日志指纹、im.elf 映射与初始环境。

    只使用相机 busybox 确实具备的 applet（cat/grep/ls/pidof/xxd/tail），
    且一律用 `busybox <applet>` 调用——相机的 PATH 里没有这些符号链接，
    也没有 tr/od，因此二进制字段统一用 `xxd -p` 交给主机侧解码。
    """
    print("[只读] 采集 preload 相关证据（不写入任何文件）...")
    script = (
        "echo '=== system ==='; "
        "echo uptime=$(cat /proc/uptime 2>/dev/null || echo NONE); "
        "cat /proc/sys/kernel/random/boot_id 2>/dev/null || echo NO_BOOT_ID; "
        "cat /proc/cmdline 2>/dev/null || echo NO_CMDLINE; "
        "grep -E ' /setting | /usr ' /proc/mounts 2>/dev/null || true; "
        "ls -l /lib/ld-linux.so.3 /lib/libc.so.6 /lib/libdl.so.2 2>/dev/null || true; "
        "echo '=== binaries ==='; "
        "busybox sha256sum /usr/bin/bootin.elf /usr/bin/im.elf 2>/dev/null || true; "
        "echo '=== init namespace view ==='; "
        "ls -l /proc/1/ns/mnt /proc/self/ns/mnt 2>/dev/null || true; "
        "ls -l /proc/1/root /proc/self/root 2>/dev/null || true; "
        "echo 'init dmode:' $(cat /proc/1/root/setting/mode/dmode 2>/dev/null || echo UNREADABLE); "
        "echo 'init preload:' $(cat /proc/1/root/setting/mode/preload 2>/dev/null || echo UNREADABLE); "
        "grep -E ' /setting ' /proc/1/mounts 2>/dev/null || true; "
        "echo '=== dmesg tail ==='; busybox dmesg 2>/dev/null | busybox tail -n 15 || true; "
        "echo '=== dmesg boot ==='; busybox dmesg 2>/dev/null | busybox grep -iE 'setting|mount|bootin|init' | busybox tail -n 25 || true; "
        "echo '=== setting files ==='; ls -la /setting 2>/dev/null || true; "
        "echo '--- env.txt ---'; cat /setting/env.txt 2>/dev/null || echo NO_ENV_TXT; "
        "echo '=== ssboot ==='; ls -la /proc/ssboot 2>/dev/null || echo NO_SSBOOT_PROC; "
        "for f in mode operation resmode state section; do echo \"ssboot.$f=$(busybox timeout 1 cat /proc/ssboot/$f 2>/dev/null || echo TIMEOUT_OR_EMPTY)\"; done; "
        "ls -la /proc/ssboot/optimizer /proc/ssboot/region 2>/dev/null || true; "
        "ls -la /system/sabin/ssboot*.bin 2>/dev/null || true; "
        "echo '=== mode files ==='; "
        "ls -la /setting/mode/ 2>/dev/null || true; "
        "echo dmode=$(cat /setting/mode/dmode 2>/dev/null || echo NONE); "
        "echo preload=$(cat /setting/mode/preload 2>/dev/null || echo NONE); "
        "echo kemco=$(grep -m1 '^warm\\.mode=' /setting/kemco.txt 2>/dev/null || echo NONE); "
        "echo -n 'dmode hex: '; busybox xxd -p /setting/mode/dmode 2>/dev/null || true; "
        "echo -n 'preload hex: '; busybox xxd -p /setting/mode/preload 2>/dev/null || true; "
        "echo '=== /usr/tool ==='; "
        "ls -la /usr/tool 2>/dev/null || true; "
        "echo '=== boot log (ulogio) ==='; "
        "ls -l /setting/ulogio.bin 2>/dev/null || echo NO_ULOGIO_BIN; "
        "grep -aE 'LD_PRELOAD|libzve10plugin|mcheckb' /setting/ulogio.bin 2>/dev/null "
        "| busybox tail -n 20 || true; "
        "echo ULOGIO_HEX_BEGIN; busybox xxd -p -l 16384 /setting/ulogio.bin 2>/dev/null || echo NO_XXD; "
        "echo ULOGIO_HEX_END; "
        "echo '=== plugin file ==='; "
        "ls -l /tmp/libzve10plugin.so 2>/dev/null || echo NO_PLUGIN_FILE; "
        "echo '=== im.elf ==='; "
        "PID=$(pidof im.elf); echo IM_PID=$PID; "
        'if [ -n "$PID" ]; then '
        "  echo '-- LD_PRELOAD in initial environment --'; "
        "  echo ENVIRON_HEX_BEGIN; busybox xxd -p /proc/$PID/environ 2>/dev/null || echo NO_XXD; "
        "  echo ENVIRON_HEX_END; "
        "  echo '-- plugin mappings (count) --'; "
        "  grep -c libzve10plugin /proc/$PID/maps 2>/dev/null || true; "
        "  echo '-- proc stat --'; "
        "  cat /proc/$PID/stat 2>/dev/null || true; "
        "  echo '-- cmdline --'; "
        "  echo CMDLINE_BEGIN; busybox xxd -p /proc/$PID/cmdline 2>/dev/null || true; echo CMDLINE_END; "
        "fi; "
        "echo '=== plugin log tail ==='; "
        "busybox tail -n 20 /tmp/zve10plugin.log 2>/dev/null || echo NO_PLUGIN_LOG; "
        "echo '=== boot guard ==='; "
        "if [ -f /tmp/zvp_boot_guard ]; then "
        "  echo GUARD_HEX_BEGIN; busybox xxd -p /tmp/zvp_boot_guard 2>/dev/null || echo NO_XXD; "
        "  echo GUARD_HEX_END; "
        "else echo NO_BOOT_GUARD_STATE; fi"
    )
    res = run_cam(["sh", script])
    visible = re.sub(r"ENVIRON_HEX_BEGIN\r?\n.*?ENVIRON_HEX_END\r?\n?", "",
                     res.stdout, flags=re.DOTALL)
    visible = re.sub(r"GUARD_HEX_BEGIN\r?\n.*?GUARD_HEX_END\r?\n?", "",
                     visible, flags=re.DOTALL)
    visible = re.sub(r"ULOGIO_HEX_BEGIN\r?\n.*?ULOGIO_HEX_END\r?\n?", "",
                     visible, flags=re.DOTALL)
    visible = re.sub(r"CMDLINE_BEGIN\r?\n.*?CMDLINE_END\r?\n?", "",
                     visible, flags=re.DOTALL)
    print(visible.rstrip())
    if res.stderr:
        print(res.stderr)

    if "kemco=warm.mode=2" in res.stdout:
        print("[warm] 当前为 warm.mode=2 暖启动镜像：bootin.elf 不会执行，preload 不生效；"
              "需要冷启动时执行 --enable-coldboot。")
    elif "kemco=warm.mode=0" in res.stdout:
        print("[warm] 当前为 warm.mode=0 冷启动持久加载。")

    ulogio = _hex_block(res.stdout, "ULOGIO_HEX")
    if ulogio:
        strings = [s.decode("ascii") for s in re.findall(rb"[ -~]{4,}", ulogio)]
        keywords = ("LD_PRELOAD", "libzve10plugin", "mcheckb", "bootin", "preload", "im.elf")
        hits = [s for s in strings if any(k.lower() in s.lower() for k in keywords)]
        if hits:
            print("[ulogio] 相关字符串:")
            for line in hits[-20:]:
                print(f"  {line}")
        else:
            print(f"[ulogio] 未发现 preload 相关字符串（共 {len(strings)} 个可读串，{len(ulogio)} 字节）")
            for line in strings[:15]:
                print(f"  | {line}")
    elif "NO_XXD" in res.stdout:
        print("[ulogio] 相机 busybox 缺少 xxd，无法扫描原始日志")

    environ = _hex_block(res.stdout, "ENVIRON_HEX")
    if environ is not None:
        entries = [item.decode("ascii", "replace") for item in environ.split(b"\0") if item]
        entry = next((item for item in entries if item.startswith("LD_PRELOAD=")), None)
        if entry:
            print(f"[环境] {entry}")
        else:
            shown = ", ".join(entries[:20]) if entries else "(空)"
            print(f"[环境] exec 时无 LD_PRELOAD（共 {len(entries)} 项: {shown}）")
    elif "NO_XXD" in res.stdout:
        print("[环境] 相机 busybox 缺少 xxd，无法解析 environ")

    hashes = dict(re.findall(r"^([0-9a-f]{64})\s+(\S+)$", res.stdout, re.MULTILINE))
    for path, expected in (("/usr/bin/bootin.elf", preload_gate.BOOTIN_ELF_SHA256_FW203),
                           ("/usr/bin/im.elf", IM_ELF_SHA256_FW203)):
        actual = hashes.get(path)
        if actual:
            verdict = "MATCH" if actual == expected else "MISMATCH"
            print(f"[校验] {path}: {verdict} ({actual[:16]}…)")

    guard = _hex_block(res.stdout, "GUARD_HEX")
    if guard is not None and len(guard) >= 16:
        magic, version, fails, trips = struct.unpack("<IIII", guard[:16])
        print(f"[看门狗] magic=0x{magic:08x} version={version} "
              f"fail_count={fails} trip_count={trips}")
        if magic != 0x5A564247:
            print("  [注意] magic 不匹配，状态文件不是本插件写的")
        elif fails >= 1:
            print("  [注意] fail_count >= 1：上次插件会话未正常收尾；"
                  "累计到 2 会自熔断并清空 preload。")
            print("         需要干净验证时，先执行: --reset-boot-guard")
    elif "NO_BOOT_GUARD_STATE" in res.stdout:
        print("[看门狗] 尚无状态文件（首次加载创建，fail_count 从 0 开始）")

    uptime_match = re.search(r"^uptime=([0-9.]+)", res.stdout, re.MULTILINE)
    stat_match = re.search(r"^\d+ \(.*\) (.*)$", res.stdout, re.MULTILINE)
    if uptime_match and stat_match:
        fields = stat_match.group(1).split()
        if len(fields) > 19:
            try:
                start_seconds = int(fields[19]) / 100.0
                print(f"[进程] im.elf 启动于开机后 {start_seconds:.1f}s（当前 uptime={uptime_match.group(1)}s）")
            except ValueError:
                pass

    cmdline_hex = _hex_block(res.stdout, "CMDLINE")
    if cmdline_hex:
        parts = [item.decode("ascii", "replace") for item in cmdline_hex.split(b"\0") if item]
        print(f"[进程] im.elf argv = {parts}")
    return res.returncode


def cmd_reset_boot_guard() -> int:
    """删除插件自己的运行期看门狗状态（/tmp，非原厂配置）。

    该文件记录 fail_count/trip_count；累计 >=2 会让插件在下次加载时
    自行写回 dmode=3 并清空 preload，干扰真机验证。
    """
    print("[写入] 重置插件运行期看门狗状态 /tmp/zvp_boot_guard ...")
    command = (
        "rm -f /tmp/zvp_boot_guard; "
        "if [ -f /tmp/zvp_boot_guard ]; then echo STILL_EXISTS; false; "
        "else echo GUARD_RESET_OK; fi"
    )
    res = run_cam(["sh", command])
    print(res.stdout)
    if res.stderr:
        print(res.stderr)
    return res.returncode


def cmd_disable_preload() -> int:
    print("恢复原厂默认启动模式 (change_mode.sh 3) 并清空 preload...")
    res = run_cam(
        ["sh", "change_mode.sh 3; echo -n > /setting/mode/preload; sync; "
               "echo dmode=$(cat /setting/mode/dmode 2>/dev/null); "
               "echo preload=$(cat /setting/mode/preload 2>/dev/null)"],
        timeout=40.0,
    )
    print(res.stdout.rstrip())
    if res.returncode != 0:
        print(res.stderr)
    else:
        print("已取消 preload 注入，重启后恢复纯原厂行为。")
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


def cmd_enable_coldboot() -> int:
    return _switch_kemco_warm_mode("0")


def cmd_restore_warmboot() -> int:
    return _switch_kemco_warm_mode("2")


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


def cmd_set_mode(mode: str, value: str | None = None) -> int:
    """诊断用：直接调用原厂 change_mode.sh（只写 /setting/mode，不碰固件）。"""
    if not re.fullmatch(r"[0-9a-fp]{1,2}", mode):
        print(f"[错误] 非法模式参数: {mode!r}")
        return 2
    if mode == "p" and not value:
        print("[错误] 模式 p 需要 --preload-value")
        return 2
    if mode == "p":
        command = (
            f'change_mode.sh p "{value}"; D=$(cat /setting/mode/dmode 2>/dev/null); '
            'P=$(cat /setting/mode/preload 2>/dev/null); echo "dmode=$D"; echo "preload=$P"'
        )
    else:
        command = (
            f'change_mode.sh {mode}; D=$(cat /setting/mode/dmode 2>/dev/null); '
            'P=$(cat /setting/mode/preload 2>/dev/null); echo "dmode=$D"; echo "preload=$P"'
        )
    print(f"[写入] change_mode.sh {mode} ...")
    res = run_cam(["sh", command])
    print(res.stdout.rstrip())
    if res.stderr:
        print(res.stderr)
    if res.returncode == 0 and "Please reboot" in res.stdout:
        print("[提示] 配置已写入，需要重启后生效。")
    return res.returncode


def cmd_write_dmode(dmode: str, preload: str | None = None) -> int:
    """诊断用：直接写 /setting/mode/dmode（可选 preload），仅用于真机差分实验。"""
    if not re.fullmatch(r"[0-9A-Za-z]{1,10}", dmode or ""):
        print(f"[错误] 非法 dmode: {dmode!r}")
        return 2
    if preload is not None and not re.fullmatch(r"[0-9A-Za-z._:/+-]{0,400}", preload):
        print(f"[错误] 非法 preload: {preload!r}")
        return 2
    if preload is None:
        command = (
            f'echo {dmode} > /setting/mode/dmode; '
            'D=$(cat /setting/mode/dmode 2>/dev/null); P=$(cat /setting/mode/preload 2>/dev/null); '
            'echo "dmode=$D"; echo "preload=$P"'
        )
    else:
        command = (
            f'echo {dmode} > /setting/mode/dmode; '
            f'echo -n "{preload}" > /setting/mode/preload; '
            'D=$(cat /setting/mode/dmode 2>/dev/null); P=$(cat /setting/mode/preload 2>/dev/null); '
            'echo "dmode=$D"; echo "preload=$P"'
        )
    print(f"[写入] dmode={dmode} preload={preload!r} ...")
    res = run_cam(["sh", command])
    print(res.stdout.rstrip())
    if res.stderr:
        print(res.stderr)
    return res.returncode


def cmd_write_env_txt(value: str) -> int:
    """诊断用：写 /setting/env.txt（init 启动时逐行 putenv 的文件）。"""
    if not re.fullmatch(r"[0-9A-Za-z_./:=+-]{1,200}", value or ""):
        print(f"[错误] 非法 env 行: {value!r}")
        return 2
    print(f"[写入] /setting/env.txt <- {value}")
    command = (
        f'echo "{value}" > /setting/env.txt; '
        'echo "env.txt=$(cat /setting/env.txt 2>/dev/null)"; '
        'ls -la /setting/env.txt 2>/dev/null || true'
    )
    res = run_cam(["sh", command])
    print(res.stdout.rstrip())
    if res.stderr:
        print(res.stderr)
    return res.returncode


def cmd_rm_env_txt() -> int:
    """诊断收尾：删除哨兵测试写入的 /setting/env.txt。"""
    print("[删除] /setting/env.txt")
    res = run_cam(["sh", "rm -f /setting/env.txt; ls -la /setting/env.txt 2>/dev/null || echo ENV_TXT_GONE"])
    print(res.stdout.rstrip())
    if res.stderr:
        print(res.stderr)
    return res.returncode


def cmd_kallsyms() -> int:
    """只读：导出内核 ssboot/upm 符号地址，用于静态映射。"""
    script = (
        "echo '=== kallsyms head ==='; sed -n '1,3p' /proc/kallsyms; "
        "echo '=== ssboot/upm symbols ==='; "
        "grep -E ' (ssboot|upm)' /proc/kallsyms | sed -n '1,120p'; "
    )
    res = run_cam(["sh", script])
    print(res.stdout.rstrip())
    if res.stderr:
        print(res.stderr)
    return res.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="Sony ZV-E10 运行期探针与部署助手")
    parser.add_argument("--check", action="store_true", help="检查相机连接与 im.elf 映射，Jiritsu 会临时上传加载器")
    parser.add_argument("--stage-tmp", action="store_true", help="上传 libzve10plugin.so 到相机 /tmp")
    parser.add_argument("--logs", action="store_true", help="读取 /tmp/zve10plugin.log")
    parser.add_argument("--preload-status", action="store_true",
                        help="只读核对 preload 配置、开机日志指纹与 im.elf 映射")
    parser.add_argument("--clean-loader", action="store_true",
                        help="检查并删除上次异常会话残留的 /tmp/jiritsu loader 文件（Jiritsu）")
    parser.add_argument("--reset-boot-guard", action="store_true",
                        help="删除 /tmp/zvp_boot_guard（插件运行期看门狗状态，非原厂配置）")
    parser.add_argument("--enable-preload", action="store_true", help="设置 /setting/mode/preload (重启后生效)")
    parser.add_argument("--disable-preload", action="store_true", help="恢复默认 mode3 无注入")
    parser.add_argument("--enable-coldboot", action="store_true",
                        help="备份 kemco.txt 后把 warm.mode 切为 0（冷启动持久加载，约 1 秒开机）")
    parser.add_argument("--restore-warmboot", action="store_true",
                        help="把 kemco.txt 的 warm.mode 还原原厂 2（暖启动镜像模式）")
    parser.add_argument("--set-mode", metavar="MODE", default=None,
                        help="诊断：直接调用 change_mode.sh <MODE>（如 10=mcheckb, 7=LeakTracer）")
    parser.add_argument("--write-dmode", metavar="TEXT", default=None,
                        help="诊断：直接写 /setting/mode/dmode（如 3pd，配合 --write-preload）")
    parser.add_argument("--write-preload", metavar="TEXT", default=None, help="配合 --write-dmode 写 preload")
    parser.add_argument("--write-env-txt", metavar="TEXT", default=None,
                        help="诊断：写 /setting/env.txt（init 启动时 putenv 的哨兵测试）")
    parser.add_argument("--rm-env-txt", action="store_true", help="删除哨兵测试写入的 /setting/env.txt")
    parser.add_argument("--kallsyms", action="store_true", help="只读：导出内核 ssboot/upm 符号地址")
    parser.add_argument("--preload-value", default=None, help="配合 --set-mode p 使用")
    parser.add_argument("--reboot", action="store_true", help="重启相机并等待 30 秒重新连线")
    parser.add_argument("--backend", choices=("camtool", "jiritsu"), default="camtool",
                        help="2.03 使用 jiritsu，默认保留 camtool 路径")
    parser.add_argument("--deploy", action="store_true", help="Jiritsu 单会话检查、上传并启用插件")
    parser.add_argument("--jiritsu-worker", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--wbi-status", action="store_true",
                        help="Jiritsu 只读：WBI 头 + ssboot/UPM 运行态（不改任何东西）")
    parser.add_argument("--wbi-prepare", action="store_true",
                        help="Jiritsu：整分区双读并比对，生成写入前的备份与清单")
    parser.add_argument("--wbi-drop", action="store_true",
                        help="Jiritsu：用原厂 ud_wbi_drop.elf 清零 WBI 头 512 B（需 --i-authorize-emmc-write）")
    parser.add_argument("--wbi-verify", action="store_true",
                        help="Jiritsu 只读：冷启动验收（无 WARMBOOT / im.elf 映射 / 插件日志）")
    parser.add_argument("--wbi-dir", default=None, help="WBI 备份与清单目录（prepare/drop 必填）")
    parser.add_argument("--wbi-tool", default=str(Path(__file__).with_name("payload") / "ud_wbi_drop.elf"),
                        help="原厂 ud_wbi_drop.elf 路径（默认 tools/payload）")
    parser.add_argument("--baseline", default=None,
                        help="已知可信的整分区备份；提供时 prepare 只做一次全读并与它逐字节比对")
    parser.add_argument("--i-authorize-emmc-write", action="store_true",
                        help="机主对 eMMC 写入的显式授权；--wbi-drop 缺少它时拒绝执行")
    parser.add_argument("--so", default=DEFAULT_SO, help="libzve10plugin.so 路径")
    parser.add_argument("--cam-py", default=None, help="指定 camtool cam.py 路径（也可设 ZVP_CAM_PY）")
    args = parser.parse_args()

    args.wbi_action = None
    for flag, name in (("wbi_status", "status"), ("wbi_prepare", "prepare"),
                       ("wbi_drop", "drop"), ("wbi_verify", "verify")):
        if getattr(args, flag):
            if args.wbi_action:
                parser.error("每次只执行一个 WBI 动作")
            args.wbi_action = name
    if args.wbi_action:
        if args.backend != "jiritsu":
            parser.error("WBI 动作只支持 --backend jiritsu（2.03）")
        if args.wbi_action in ("prepare", "drop") and not args.wbi_dir:
            parser.error("--wbi-prepare/--wbi-drop 需要 --wbi-dir <备份目录>")
        if args.wbi_action == "drop" and not args.i_authorize_emmc_write:
            parser.error("--wbi-drop 会写 eMMC。确认后加 --i-authorize-emmc-write 再执行")
        tool = Path(args.wbi_tool)
        if args.wbi_action == "drop" and not tool.is_file():
            parser.error(f"找不到原厂工具 {tool}；先用 extract 步骤放入 tools/payload/")

    if args.backend == "jiritsu":
        if args.enable_coldboot or args.restore_warmboot:
            parser.error("kemco warm.mode 切换只支持 camtool 后端；请去掉 --backend jiritsu（camtool 的 /tmp/jiritsu 通道同样兼容 2.03）")
        if args.cam_py:
            parser.error("--cam-py 仅适用于 camtool 后端")
        if not args.jiritsu_worker:
            env = os.environ.copy()
            env["PYTHONIOENCODING"] = "utf-8"
            command = ["uv", "run", "--with", "pyusb", "--with", "libusb-package",
                       "--with", "pycryptodomex", "--with", "pyelftools", "python", os.path.abspath(__file__),
                       *sys.argv[1:], "--jiritsu-worker"]
            return subprocess.run(command, env=env).returncode
        from zve10_jiritsu_runtime import run
        return run(args)
    if args.deploy:
        parser.error("--deploy 需要 --backend jiritsu")
    if args.clean_loader:
        parser.error("--clean-loader 需要 --backend jiritsu")

    global CAM_PY
    if args.cam_py:
        CAM_PY = resolve_cam_py(args.cam_py)

    if args.check:
        return cmd_check()
    if args.stage_tmp:
        return cmd_stage_tmp(args.so)
    if args.logs:
        return cmd_logs()
    if args.preload_status:
        return cmd_preload_status()
    if args.reset_boot_guard:
        return cmd_reset_boot_guard()
    if args.enable_preload:
        return cmd_enable_preload()
    if args.disable_preload:
        return cmd_disable_preload()
    if args.enable_coldboot:
        return cmd_enable_coldboot()
    if args.restore_warmboot:
        return cmd_restore_warmboot()
    if args.set_mode is not None:
        return cmd_set_mode(args.set_mode, args.preload_value)
    if args.write_dmode is not None:
        return cmd_write_dmode(args.write_dmode, args.write_preload)
    if args.write_env_txt is not None:
        return cmd_write_env_txt(args.write_env_txt)
    if args.rm_env_txt:
        return cmd_rm_env_txt()
    if args.kallsyms:
        return cmd_kallsyms()
    if args.reboot:
        return cmd_reboot(30)

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
