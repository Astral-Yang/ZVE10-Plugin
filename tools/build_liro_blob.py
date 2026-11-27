#!/usr/bin/env python3
"""将 liro/zvp_liro_hook.c 编译为 VMA=0x6471AD00 纯平二进制，并生成 include/zvp_liro_blob.h。"""
from __future__ import annotations

import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SRC = os.path.join(ROOT, "liro", "zvp_liro_hook.c")
LD = os.path.join(ROOT, "liro", "zvp_liro.ld")
INC = os.path.join(ROOT, "include")
ELF_OUT = os.path.join(ROOT, "build-arm", "zvp_liro_hook.elf")
BIN_OUT = os.path.join(ROOT, "build-arm", "zvp_liro_hook.bin")
HDR_OUT = os.path.join(ROOT, "include", "zvp_liro_blob.h")


def win_to_wsl(p: str) -> str:
    p = os.path.abspath(p).replace("\\", "/")
    if len(p) >= 2 and p[1] == ":":
        return f"/mnt/{p[0].lower()}{p[2:]}"
    return p


def main() -> int:
    os.makedirs(os.path.dirname(ELF_OUT), exist_ok=True)
    cmd_gcc = [
        "wsl", "arm-linux-gnueabi-gcc",
        "-march=armv7-a", "-mthumb", "-mfloat-abi=soft",
        "-ffreestanding", "-nostdlib", "-O2", "-Wall", "-Wextra",
        f"-I{win_to_wsl(INC)}",
        "-T", win_to_wsl(LD),
        win_to_wsl(SRC),
        "-o", win_to_wsl(ELF_OUT),
    ]
    subprocess.run(cmd_gcc, check=True)

    cmd_objcopy = [
        "wsl", "arm-linux-gnueabi-objcopy",
        "-O", "binary",
        win_to_wsl(ELF_OUT),
        win_to_wsl(BIN_OUT),
    ]
    subprocess.run(cmd_objcopy, check=True)

    dis = subprocess.run(
        ["wsl", "arm-linux-gnueabi-objdump", "-d", win_to_wsl(ELF_OUT)],
        capture_output=True, text=True, check=True
    )
    print(dis.stdout)

    blob = open(BIN_OUT, "rb").read()
    lines = [
        "#ifndef ZVP_LIRO_BLOB_H",
        "#define ZVP_LIRO_BLOB_H",
        "",
        "#include <stdint.h>",
        "",
        "/* 自动生成自 liro/zvp_liro_hook.c (VMA=0x6471AD00, PA=0x0471AD00) */",
        f"#define ZVP_LIRO_BLOB_SIZE {len(blob)}u",
        "",
        "static const uint8_t zvp_liro_blob_bytes[] = {",
    ]
    for i in range(0, len(blob), 12):
        chunk = ", ".join(f"0x{b:02X}u" for b in blob[i : i + 12])
        lines.append(f"    {chunk},")
    lines.extend(["};", "", "#endif /* ZVP_LIRO_BLOB_H */", ""])
    with open(HDR_OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"[OK] Generated {HDR_OUT} ({len(blob)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
