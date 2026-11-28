#!/usr/bin/env python3
"""tools/probe_zve10_runtime.py 的离线单元测试（无需连接真机 USB）。"""
from __future__ import annotations

import os
import re
import struct
import sys
import tempfile
import unittest
from unittest import mock

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
TOOLS_DIR = os.path.join(REPO_ROOT, "tools")
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import probe_zve10_runtime as probe


SAMPLE_KEMCO = (
    b"kernel.log=0\r\n"
    b"warm.mode=2\r\n"
    b"warm.device=/dev/nflasha5\r\n"
)


class ProbeRuntimeTests(unittest.TestCase):
    def test_kemco_warm_mode_roundtrip_exact_1_byte(self) -> None:
        cur, off = probe.parse_kemco_warm_mode(SAMPLE_KEMCO)
        self.assertEqual(cur, "2")
        self.assertEqual(SAMPLE_KEMCO[off : off + 1], b"2")

        cold = probe.patch_kemco_warm_mode(SAMPLE_KEMCO, "0")
        self.assertEqual(len(cold), len(SAMPLE_KEMCO))
        self.assertEqual(sum(1 for a, b in zip(SAMPLE_KEMCO, cold) if a != b), 1)
        self.assertEqual(probe.parse_kemco_warm_mode(cold)[0], "0")
        self.assertIn(b"\r\nwarm.mode=0\r\n", cold)

        # 幂等性
        self.assertEqual(probe.patch_kemco_warm_mode(cold, "0"), cold)

        # 可逆还原
        warm = probe.patch_kemco_warm_mode(cold, "2")
        self.assertEqual(warm, SAMPLE_KEMCO)

    def test_kemco_rejects_invalid_or_duplicate_entries(self) -> None:
        with self.assertRaises(ValueError):
            probe.parse_kemco_warm_mode(b"foo=1\nbar=2\n")
        with self.assertRaises(ValueError):
            probe.parse_kemco_warm_mode(b"warm.mode=2\nwarm.mode=0\n")
        with self.assertRaises(ValueError):
            probe.patch_kemco_warm_mode(SAMPLE_KEMCO, "1")
        with self.assertRaises(ValueError):
            probe.patch_kemco_warm_mode(b"warm.mode=9\n", "0")

    def test_extract_cmdline_and_boot_guard(self) -> None:
        self.assertEqual(
            probe.extract_cmdline_warm_mode("console=ttyS0 warm.mode=2 root=/dev/nflasha15"),
            "2",
        )
        self.assertIsNone(probe.extract_cmdline_warm_mode("console=ttyS0 quiet"))

        raw_bg = struct.pack("<IIII", probe.ZVP_BOOT_GUARD_MAGIC, 1, 2, 3)
        bg = probe.parse_boot_guard_bytes(raw_bg)
        self.assertIsNotNone(bg)
        self.assertEqual(bg["magic_ok"], 1)
        self.assertEqual(bg["fail_count"], 2)
        self.assertEqual(bg["trip_count"], 3)

    def test_stage_push_file_uses_jiritsu_staging_for_non_jiritsu_paths(self) -> None:
        calls: list[list[str]] = []

        def fake_run_cam(args, timeout=60.0):
            calls.append(args)
            res = mock.MagicMock()
            res.returncode = 0
            res.stdout = "-rwxr-xr-x    1 0        0               5 Jan  1 00:00 /tmp/libzve10plugin.so\n[exit=0]\n" if args[0] == "sh" else "OK\n"
            res.stderr = ""
            return res

        fd, tmp_file = tempfile.mkstemp(prefix="zvp_ut_", suffix=".so")
        os.close(fd)
        try:
            with open(tmp_file, "wb") as f:
                f.write(b"12345")
            with mock.patch.object(probe, "run_cam", side_effect=fake_run_cam):
                probe.stage_push_file(tmp_file, "/tmp/libzve10plugin.so", file_mode="0755")
        finally:
            os.remove(tmp_file)

        self.assertEqual(len(calls), 2)
        self.assertEqual(calls[0][0], "put")
        self.assertTrue(calls[0][2].startswith("/tmp/jiritsu/_zvp_stage_"))
        self.assertEqual(calls[1][0], "sh")
        self.assertIn("/tmp/libzve10plugin.so", calls[1][1])
        self.assertIn("mv ", calls[1][1])

    def test_switch_kemco_warm_mode_backups_and_rolls_back_on_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_bak_dir:
            pushed: list[bytes] = []

            def fake_pull(remote_path: str) -> bytes:
                self.assertEqual(remote_path, probe.REMOTE_KEMCO_PATH)
                # 第一次调用返回原厂内容，第二次回读也返回修改后的内容（测试成功链路）
                return SAMPLE_KEMCO if not pushed else pushed[-1]

            def fake_push(local_path: str, remote_path: str, file_mode: str = "0644") -> None:
                self.assertEqual(remote_path, probe.REMOTE_KEMCO_PATH)
                with open(local_path, "rb") as f:
                    pushed.append(f.read())

            with mock.patch.object(probe, "BACKUP_KEMCO_DIR", tmp_bak_dir), \
                 mock.patch.object(probe, "stage_pull_bytes", side_effect=fake_pull), \
                 mock.patch.object(probe, "stage_push_file", side_effect=fake_push):
                rc = probe._switch_kemco_warm_mode("0")

            self.assertEqual(rc, 0)
            self.assertEqual(len(pushed), 1)
            self.assertEqual(probe.parse_kemco_warm_mode(pushed[0])[0], "0")
            self.assertTrue(os.path.isfile(os.path.join(tmp_bak_dir, "kemco.txt.orig")))

            # 回读不一致时应触发自动回滚写回原厂 SAMPLE_KEMCO
            pushed_bad: list[bytes] = []
            pull_seq = [SAMPLE_KEMCO, b"corrupted\nwarm.mode=0\n"]

            def fake_push_rb(local_path: str, remote_path: str, file_mode: str = "0644") -> None:
                with open(local_path, "rb") as f:
                    pushed_bad.append(f.read())

            with mock.patch.object(probe, "BACKUP_KEMCO_DIR", tmp_bak_dir), \
                 mock.patch.object(probe, "stage_pull_bytes", side_effect=pull_seq), \
                 mock.patch.object(probe, "stage_push_file", side_effect=fake_push_rb):
                rc2 = probe._switch_kemco_warm_mode("0")

            self.assertEqual(rc2, 2)
            self.assertEqual(len(pushed_bad), 2)
            self.assertEqual(pushed_bad[-1], SAMPLE_KEMCO)

    def test_shell_scripts_survive_the_jiritsu_command_guard(self) -> None:
        """探针下发的 shell 脚本不能包含整词 exit/exec/kill/trap/return（Jiritsu 常驻 shell 会拒绝）。"""
        banned = re.compile(r"\b(exit|exec|kill|trap|return)\b")
        captured: list[str] = []

        def fake_run_cam(args, capture: bool = True, timeout: float | None = None):
            if args[0] == "sh":
                captured.append(args[1])
            res = mock.MagicMock()
            res.returncode = 0
            res.stderr = ""
            res.stdout = "connected\n" if args[0] == "doctor" else "IM_PID=123\ncmdline=warm.mode=0\n"
            return res

        baseline = mock.MagicMock(returncode=0, stderr="",
                                  stdout="NETSERVICE_BYTES=90b595b0\nCAUTION_BYTES=20\n")
        with mock.patch.object(probe, "run_cam", side_effect=fake_run_cam), \
             mock.patch.object(probe, "read_firmware_bytes", return_value=baseline):
            probe.cmd_check()

        with tempfile.NamedTemporaryFile(suffix=".so") as source:
            with mock.patch.object(probe, "run_cam", side_effect=fake_run_cam), \
                 mock.patch.object(probe, "run_verify_abi", return_value=0), \
                 mock.patch.object(probe, "stage_push_file"):
                probe.cmd_stage_tmp(source.name)

        # Jiritsu 后端支持的所有探针命令都走 run_cam(["sh", ...])，一并覆盖
        with mock.patch.object(probe, "run_cam", side_effect=fake_run_cam):
            probe.cmd_logs()
            probe.cmd_preload_status()
            probe.cmd_reset_boot_guard()
            probe.cmd_enable_preload()
            probe.cmd_disable_preload()
            probe.cmd_set_mode("3")
            probe.cmd_write_dmode("3pd")
            probe.cmd_write_env_txt("FOO=bar")
            probe.cmd_rm_env_txt()
            probe.cmd_kallsyms()

        self.assertTrue(captured)
        for script in captured:
            self.assertIsNone(banned.search(script), script)

    def test_read_firmware_bytes_has_no_bare_od_dependency(self) -> None:
        captured: list[list[str]] = []

        def fake_run_cam(args, capture: bool = True, timeout: float | None = None):
            captured.append(args)
            res = mock.MagicMock()
            res.returncode = 0
            res.stdout = ""
            res.stderr = ""
            return res

        with mock.patch.object(probe, "run_cam", side_effect=fake_run_cam):
            probe.read_firmware_bytes()

        script = captured[0][1]
        self.assertIn("busybox dd if=/usr/lib/libNetService.so", script)
        self.assertIn("busybox xxd", script)
        # 回退也必须是 busybox od，不能依赖相机 PATH 里的独立 od
        self.assertNotIn("| od ", script)

    def test_check_tolerates_missing_backup_dir_and_parses_hex_baseline(self) -> None:
        def fake_run_cam(args, capture: bool = True, timeout: float | None = None):
            res = mock.MagicMock()
            res.returncode = 0
            res.stderr = ""
            if args[0] == "doctor":
                res.stdout = "connected\n"
            else:
                res.stdout = "ZVP_CAM_OK\nIM_PID=123\ncmdline=warm.mode=0\n"
            return res

        def baseline(text: str):
            return mock.MagicMock(returncode=0, stderr="", stdout=text)

        with mock.patch.object(probe, "run_cam", side_effect=fake_run_cam), \
             mock.patch.object(probe, "read_firmware_bytes",
                               return_value=baseline("NETSERVICE_BYTES=90b595b0\nCAUTION_BYTES=20\n")):
            self.assertEqual(probe.cmd_check(), 0)

        with mock.patch.object(probe, "run_cam", side_effect=fake_run_cam), \
             mock.patch.object(probe, "read_firmware_bytes",
                               return_value=baseline("NETSERVICE_BYTES=90b595b0\nCAUTION_BYTES=1b\n")):
            self.assertEqual(probe.cmd_check(), 1)


if __name__ == "__main__":
    unittest.main()
