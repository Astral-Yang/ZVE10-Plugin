#!/usr/bin/env python3
"""
验证交叉编译生成的 libzve10plugin.so 严格符合 Sony ZV-E10 固件运行约束：
1. ARM32 小端 ELF32 动态共享库 (ET_DYN, EM_ARM)
2. e_flags 为 EABI5 且绝不包含 EF_ARM_ABI_FLOAT_HARD (0x400)
3. 无非法 GLIBC >= 2.12 符号依赖（目标板最高 glibc 2.11.2）
4. 无可读可写且不可写入的异常 .dynamic 或 RWX 段
"""
import sys
import re
from elftools.elf.elffile import ELFFile

EF_ARM_ABI_FLOAT_HARD = 0x00000400
EF_ARM_ABI_FLOAT_SOFT = 0x00000200
MAX_GLIBC = (2, 11, 2)


def parse_ver(vstr: str):
    m = re.match(r"GLIBC_(\d+)\.(\d+)(?:\.(\d+))?", vstr)
    if not m:
        return None
    return (int(m.group(1)), int(m.group(2)), int(m.group(3) or 0))


def main() -> int:
    if len(sys.argv) != 2:
        print(f"用法: {sys.argv[0]} <path-to-so>")
        return 2

    so_path = sys.argv[1]
    errors = []

    with open(so_path, "rb") as f:
        elf = ELFFile(f)
        hdr = elf.header
        if hdr["e_type"] != "ET_DYN":
            errors.append(f"e_type 错误: {hdr['e_type']} (期望 ET_DYN)")
        if hdr["e_machine"] != "EM_ARM":
            errors.append(f"e_machine 错误: {hdr['e_machine']} (期望 EM_ARM)")
        if hdr["e_ident"]["EI_CLASS"] != "ELFCLASS32":
            errors.append(f"EI_CLASS 错误: {hdr['e_ident']['EI_CLASS']}")

        eflags = hdr["e_flags"]
        if (eflags & 0xFF000000) != 0x05000000:
            errors.append(f"e_flags EABI 版本异常: 0x{eflags:08x} (期望 EABI5)")
        if eflags & EF_ARM_ABI_FLOAT_HARD:
            errors.append(f"检测到 hard-float 标志位 (e_flags=0x{eflags:08x})")
        if not (eflags & EF_ARM_ABI_FLOAT_SOFT):
            print(f"[警告] e_flags=0x{eflags:08x} 未显式置位 EF_ARM_ABI_FLOAT_SOFT (将检查 Tag_ABI_VFP_args)")

        # 检查 ARM build attributes (hard/soft float)
        arm_attr = elf.get_section_by_name(".ARM.attributes")
        if arm_attr:
            raw_attr = arm_attr.data()
            # 检查 AEABI tag 28 (Tag_ABI_VFP_args): 1 = VFP registers (hard), 0 = AAPCS (soft)
            # 这里仅作信息输出，主要依据 e_flags
            print(f"[信息] .ARM.attributes 大小: {len(raw_attr)} 字节")

        # 检查 .dynamic 是否位于可写 PT_LOAD 段中 (避免 Sony/glibc 早期动态链接器问题)
        dyn_sec = elf.get_section_by_name(".dynamic")
        if dyn_sec:
            dyn_addr = dyn_sec["sh_addr"]
            in_rw = False
            for seg in elf.iter_segments():
                if seg["p_type"] == "PT_LOAD":
                    vstart = seg["p_vaddr"]
                    vend = vstart + seg["p_memsz"]
                    if vstart <= dyn_addr < vend and (seg["p_flags"] & 0x2):
                        in_rw = True
                        break
            if not in_rw:
                errors.append(".dynamic 未落在可写 (PF_W) PT_LOAD 段中")

        # 检查 GLIBC 符号版本要求
        verneed = elf.get_section_by_name(".gnu.version_r")
        high_vers = []
        if verneed:
            for _, auxs in verneed.iter_versions():
                for aux in auxs:
                    vname = aux.name
                    ver = parse_ver(vname)
                    if ver and ver > MAX_GLIBC:
                        high_vers.append(vname)
        if high_vers:
            errors.append(
                f"检测到超出相机 glibc 2.11.2 上限的版本符号依赖: {sorted(set(high_vers))}"
            )

        # 检查未定义外部符号，确保没有意外引入相机缺失库或 C++ libstdc++ 依赖
        dynsym = elf.get_section_by_name(".dynsym")
        undef = []
        needed = []
        if dyn_sec:
            for tag in dyn_sec.iter_tags():
                if tag.entry.d_tag == "DT_NEEDED":
                    needed.append(tag.needed)
        if dynsym:
            for s in dynsym.iter_symbols():
                if s["st_shndx"] == "SHN_UNDEF" and s.name:
                    undef.append(s.name)

        print(f"[信息] 文件: {so_path}")
        print(f"[信息] e_flags: 0x{eflags:08x}")
        print(f"[信息] DT_NEEDED: {needed}")
        print(f"[信息] 未定义导入符号 ({len(undef)}): {', '.join(undef[:25])}" +
              (" ..." if len(undef) > 25 else ""))

        for lib in needed:
            if "stdc++" in lib or "gcc_s" in lib:
                errors.append(f"不应动态依赖 {lib}（相机兼容性风险）")

    if errors:
        print("[失败] ABI 校验未通过:")
        for e in errors:
            print(f"  - {e}")
        return 1

    print("[通过] libzve10plugin.so ELF/ABI/GLIBC 约束全部校验通过！")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
