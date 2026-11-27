#!/usr/bin/env python3
"""
从 Windows 系统字体自动生成 ZV-E10 UI 专用精简 LVGL v9.2 4bpp 中文字体 C 文件。
自动扫描 src/*.c 与 include/*.h 中的所有字符，保证运行时零缺字方框。
"""
from __future__ import annotations

import glob
import os
from PIL import Image, ImageDraw, ImageFont

FONT_CANDIDATES = [
    r"C:\Windows\Fonts\msyh.ttc",
    r"C:\Windows\Fonts\msyh.ttf",
    r"C:\Windows\Fonts\simhei.ttf",
    r"C:\Windows\Fonts\simsun.ttc",
]

EXTRA_FALLBACK_TEXT = (
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    "°·—（）【】：，。！？/+-_% "
    "待机原厂已开启关闭解锁中等待网络服务加载自动信道频段设置与安全"
    "按键唤醒长按短按独占推荐屏幕翻转跟随保持正向菜单就绪确认复位"
    "再次点击恢复默认未检测到扩展状态运行模式仅保留屏蔽卡禁用文件提示"
    "请重启或验证版本错误成功失败卡控熔断激活插卡退出方向触控"
    "自定义快门音拍摄界面小悬浮窗音频选择立即试听播放内置清脆电子"
    "播转换检查目录按下自动启用未发现"
)


def pick_font_path() -> str:
    for p in FONT_CANDIDATES:
        if os.path.exists(p):
            return p
    raise FileNotFoundError("未找到 Windows 中文字体文件 (msyh / simhei / simsun)")


def collect_codepoints(root: str) -> list[int]:
    cps = set(range(0x20, 0x7F))
    for ch in EXTRA_FALLBACK_TEXT:
        if ord(ch) >= 0x20:
            cps.add(ord(ch))

    patterns = [
        os.path.join(root, "src", "*.c"),
        os.path.join(root, "include", "*.h"),
    ]
    for pat in patterns:
        for path in glob.glob(pat):
            base = os.path.basename(path)
            if base.startswith("zvp_font_"):
                continue
            with open(path, "r", encoding="utf-8", errors="ignore") as f:
                text = f.read()
            for ch in text:
                cp = ord(ch)
                if cp > 0x7E and not (0xE000 <= cp <= 0xF8FF) and ch not in ("\r", "\n", "\t"):
                    cps.add(cp)

    return sorted(cps)


def render_font_c(font_path: str, px_size: int, sym_name: str, out_path: str, cps: list[int]) -> None:
    font = ImageFont.truetype(font_path, px_size)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    base_line = descent

    ascii_cps = [cp for cp in cps if 0x20 <= cp <= 0x7E]
    extra_cps = [cp for cp in cps if cp > 0x7E]
    ordered_cps = ascii_cps + extra_cps

    bitmap_bytes: list[int] = []
    glyph_entries: list[tuple[int, int, int, int, int, int, int]] = []

    for cp in ordered_cps:
        ch = chr(cp)
        adv = font.getlength(ch)
        adv_w = max(1, int(round(adv * 16)))

        if cp == 0x20:
            glyph_entries.append((cp, len(bitmap_bytes), adv_w, 0, 0, 0, 0))
            continue

        bbox = font.getbbox(ch, anchor="ls")
        if not bbox:
            glyph_entries.append((cp, len(bitmap_bytes), adv_w, 0, 0, 0, 0))
            continue

        x0, y0, x1, y1 = bbox
        w = max(0, x1 - x0)
        h = max(0, y1 - y0)
        if w == 0 or h == 0:
            glyph_entries.append((cp, len(bitmap_bytes), adv_w, 0, 0, 0, 0))
            continue

        img = Image.new("L", (w, h), 0)
        draw = ImageDraw.Draw(img)
        draw.text((-x0, -y0), ch, fill=255, font=font, anchor="ls")
        px = img.tobytes()

        nibbles = [min(15, (b + 8) // 17) for b in px]
        if len(nibbles) & 1:
            nibbles.append(0)
        packed = [(nibbles[i] << 4) | nibbles[i + 1] for i in range(0, len(nibbles), 2)]

        ofs_x = x0
        ofs_y = -y1
        b_idx = len(bitmap_bytes)
        bitmap_bytes.extend(packed)
        glyph_entries.append((cp, b_idx, adv_w, w, h, ofs_x, ofs_y))

    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("/* 自动生成的 ZV-E10 UI 精简 4bpp 中文字体 (由 tools/gen_zh_font.py 生成) */\n")
        f.write('#include "lvgl.h"\n\n')

        f.write("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {\n")
        for i in range(0, len(bitmap_bytes), 16):
            chunk = ", ".join(f"0x{b:02x}" for b in bitmap_bytes[i:i + 16])
            f.write(f"    {chunk},\n")
        f.write("};\n\n")

        f.write("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {\n")
        f.write("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},\n")
        for cp, b_idx, adv_w, w, h, ox, oy in glyph_entries:
            f.write(
                f"    {{.bitmap_index = {b_idx}, .adv_w = {adv_w}, .box_w = {w}, .box_h = {h}, .ofs_x = {ox}, .ofs_y = {oy}}}, /* U+{cp:04X} */\n"
            )
        f.write("};\n\n")

        f.write("static const uint16_t unicode_list_extra[] = {\n")
        base_extra = extra_cps[0] if extra_cps else 0x80
        for i in range(0, len(extra_cps), 12):
            chunk = ", ".join(f"0x{cp - base_extra:04x}" for cp in extra_cps[i:i + 12])
            f.write(f"    {chunk},\n")
        f.write("};\n\n")

        range_len_extra = (extra_cps[-1] - base_extra + 1) if extra_cps else 1
        glyph_id_extra_start = 1 + len(ascii_cps)

        f.write("static const lv_font_fmt_txt_cmap_t cmaps[] = {\n")
        f.write("    {\n")
        f.write(f"        .range_start = {ascii_cps[0]}, .range_length = {len(ascii_cps)}, .glyph_id_start = 1,\n")
        f.write("        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0,\n")
        f.write("        .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY\n")
        f.write("    },\n")
        if extra_cps:
            f.write("    {\n")
            f.write(f"        .range_start = {base_extra}, .range_length = {range_len_extra}, .glyph_id_start = {glyph_id_extra_start},\n")
            f.write("        .unicode_list = unicode_list_extra, .glyph_id_ofs_list = NULL,\n")
            f.write(f"        .list_length = {len(extra_cps)}, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY\n")
            f.write("    },\n")
        f.write("};\n\n")

        f.write("static const lv_font_fmt_txt_dsc_t font_dsc = {\n")
        f.write("    .glyph_bitmap = glyph_bitmap,\n")
        f.write("    .glyph_dsc = glyph_dsc,\n")
        f.write("    .cmaps = cmaps,\n")
        f.write("    .kern_dsc = NULL,\n")
        f.write("    .kern_scale = 0,\n")
        f.write(f"    .cmap_num = {2 if extra_cps else 1},\n")
        f.write("    .bpp = 4,\n")
        f.write("    .kern_classes = 0,\n")
        f.write("    .bitmap_format = 0,\n")
        f.write("};\n\n")

        f.write(f"const lv_font_t {sym_name} = {{\n")
        f.write("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,\n")
        f.write("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n")
        f.write(f"    .line_height = {line_height},\n")
        f.write(f"    .base_line = {base_line},\n")
        f.write("    .subpx = LV_FONT_SUBPX_NONE,\n")
        f.write("    .underline_position = -1,\n")
        f.write("    .underline_thickness = 1,\n")
        f.write("    .dsc = &font_dsc,\n")
        f.write("    .fallback = &lv_font_montserrat_14,\n")
        f.write("};\n")

    print(f"生成 {out_path}: {len(ordered_cps)} 字形 (含 {len(extra_cps)} 非ASCII), 位图 {len(bitmap_bytes)} 字节")


def draw_lut_icon_16(idx: int) -> Image.Image:
    import math
    img = Image.new("L", (64, 64), 0)
    d = ImageDraw.Draw(img)
    if idx == 0:
        # 强度：三阶递增柱
        d.rounded_rectangle((10, 36, 20, 54), radius=3, fill=255)
        d.rounded_rectangle((27, 23, 37, 54), radius=3, fill=255)
        d.rounded_rectangle((44, 10, 54, 54), radius=3, fill=255)
    elif idx == 1:
        # 对比：经典半实心半空心圆
        d.pieslice((8, 8, 56, 56), start=90, end=270, fill=255)
        d.ellipse((8, 8, 56, 56), outline=255, width=6)
    elif idx == 2:
        # 高光：太阳圆心 + 8向光芒
        d.ellipse((21, 21, 43, 43), fill=255)
        for k in range(8):
            ang = k * math.pi / 4.0
            x0 = 32 + 16.5 * math.cos(ang)
            y0 = 32 + 16.5 * math.sin(ang)
            x1 = 32 + 26.5 * math.cos(ang)
            y1 = 32 + 26.5 * math.sin(ang)
            d.line([(x0, y0), (x1, y1)], fill=255, width=5)
    elif idx == 3:
        # 阴影：外圆 + 右下半45度斜线暗部
        hatch = Image.new("L", (64, 64), 0)
        hd = ImageDraw.Draw(hatch)
        for c in range(56, 120, 12):
            hd.line([(8, c - 8), (c - 8, 8)], fill=255, width=5)
        mask = Image.new("L", (64, 64), 0)
        md = ImageDraw.Draw(mask)
        md.pieslice((10, 10, 54, 54), start=315, end=135, fill=255)
        img.paste(hatch, (0, 0), mask)
        d.ellipse((8, 8, 56, 56), outline=255, width=6)
    elif idx == 4:
        # 褪色：黑阶抬升曲线
        d.line([(10, 10), (10, 54), (54, 54)], fill=255, width=5, joint="curve")
        d.line([(10, 39), (26, 33), (53, 12)], fill=255, width=5, joint="curve")
        d.ellipse((5, 34, 15, 44), fill=255)
    elif idx == 5:
        # 饱和：品字形三环交叠色轮
        for cx, cy in ((32, 21), (21, 41), (43, 41)):
            d.ellipse((cx - 13, cy - 13, cx + 13, cy + 13), outline=255, width=5)
    elif idx == 6:
        # 锐度：空心等腰三角形
        d.line([(32, 10), (54, 53), (10, 53), (32, 10)], fill=255, width=6, joint="curve")
    elif idx == 7:
        # 重置：逆时针环形复位箭头
        d.arc((10, 10, 54, 54), start=210, end=135, fill=255, width=6)
        d.polygon([(7, 29), (12, 11), (26, 22)], fill=255)
    return img.resize((16, 16), Image.Resampling.LANCZOS)


def draw_lut_top_icon_20(idx: int) -> Image.Image:
    img = Image.new("L", (80, 80), 0)
    d = ImageDraw.Draw(img)
    if idx == 0:
        # U+E008 关闭：经典圆形斜杠禁用符号
        d.ellipse((12, 12, 68, 68), outline=255, width=8)
        d.line([(21, 59), (59, 21)], fill=255, width=8)
    elif idx == 1:
        # U+E009 SD卡导入：斜切角SD卡轮廓 + 金手指 + 向下导入箭头
        d.line([(16, 10), (49, 10), (64, 25), (64, 70), (16, 70), (16, 10)],
               fill=255, width=7, joint="curve")
        for kx in (27, 36, 45):
            d.line([(kx, 17), (kx, 26)], fill=255, width=4)
        d.rectangle((36, 33, 44, 49), fill=255)
        d.polygon([(26, 46), (54, 46), (40, 62)], fill=255)
    return img.resize((20, 20), Image.Resampling.LANCZOS)


def append_lut_icons_c(out_path: str) -> None:
    bitmap_bytes: list[int] = []
    glyph_entries: list[tuple[int, int]] = []
    for idx in range(8):
        img = draw_lut_icon_16(idx)
        px = img.tobytes()
        nibbles = [min(15, (b + 8) // 17) for b in px]
        if len(nibbles) & 1:
            nibbles.append(0)
        packed = [(nibbles[i] << 4) | nibbles[i + 1] for i in range(0, len(nibbles), 2)]
        glyph_entries.append((0xE000 + idx, len(bitmap_bytes)))
        bitmap_bytes.extend(packed)

    top_bitmap: list[int] = []
    top_entries: list[tuple[int, int]] = []
    for idx in range(2):
        img = draw_lut_top_icon_20(idx)
        px = img.tobytes()
        nibbles = [min(15, (b + 8) // 17) for b in px]
        if len(nibbles) & 1:
            nibbles.append(0)
        packed = [(nibbles[i] << 4) | nibbles[i + 1] for i in range(0, len(nibbles), 2)]
        top_entries.append((0xE008 + idx, len(top_bitmap)))
        top_bitmap.extend(packed)

    with open(out_path, "a", encoding="utf-8", newline="\n") as f:
        f.write("\n/* 内嵌 8 个 16x16 4bpp 抗锯齿 LUT 摄影微调图标 (U+E000..U+E007) */\n")
        f.write("static LV_ATTRIBUTE_LARGE_CONST const uint8_t lut_icon_bitmap[] = {\n")
        for i in range(0, len(bitmap_bytes), 16):
            chunk = ", ".join(f"0x{b:02x}" for b in bitmap_bytes[i:i + 16])
            f.write(f"    {chunk},\n")
        f.write("};\n\n")
        f.write("static const lv_font_fmt_txt_glyph_dsc_t lut_icon_glyph_dsc[] = {\n")
        f.write("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},\n")
        for cp, b_idx in glyph_entries:
            f.write(
                f"    {{.bitmap_index = {b_idx}, .adv_w = 256, .box_w = 16, .box_h = 16, .ofs_x = 0, .ofs_y = 0}}, /* U+{cp:04X} */\n"
            )
        f.write("};\n\n")
        f.write("static const lv_font_fmt_txt_cmap_t lut_icon_cmaps[] = {\n")
        f.write("    {\n")
        f.write("        .range_start = 0xE000, .range_length = 8, .glyph_id_start = 1,\n")
        f.write("        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0,\n")
        f.write("        .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY\n")
        f.write("    },\n")
        f.write("};\n\n")
        f.write("static const lv_font_fmt_txt_dsc_t lut_icon_font_dsc = {\n")
        f.write("    .glyph_bitmap = lut_icon_bitmap,\n")
        f.write("    .glyph_dsc = lut_icon_glyph_dsc,\n")
        f.write("    .cmaps = lut_icon_cmaps,\n")
        f.write("    .kern_dsc = NULL,\n")
        f.write("    .kern_scale = 0,\n")
        f.write("    .cmap_num = 1,\n")
        f.write("    .bpp = 4,\n")
        f.write("    .kern_classes = 0,\n")
        f.write("    .bitmap_format = 0,\n")
        f.write("};\n\n")
        f.write("const lv_font_t zvp_font_lut_icons = {\n")
        f.write("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,\n")
        f.write("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n")
        f.write("    .line_height = 16,\n")
        f.write("    .base_line = 0,\n")
        f.write("    .subpx = LV_FONT_SUBPX_NONE,\n")
        f.write("    .underline_position = -1,\n")
        f.write("    .underline_thickness = 1,\n")
        f.write("    .dsc = &lut_icon_font_dsc,\n")
        f.write("    .fallback = NULL,\n")
        f.write("};\n")

        f.write("\n/* 内嵌 2 个 20x20 4bpp 抗锯齿 LUT 顶栏图标 (U+E008=关闭, U+E009=SD卡导入) */\n")
        f.write("static LV_ATTRIBUTE_LARGE_CONST const uint8_t lut_top_icon_bitmap[] = {\n")
        for i in range(0, len(top_bitmap), 16):
            chunk = ", ".join(f"0x{b:02x}" for b in top_bitmap[i:i + 16])
            f.write(f"    {chunk},\n")
        f.write("};\n\n")
        f.write("static const lv_font_fmt_txt_glyph_dsc_t lut_top_icon_glyph_dsc[] = {\n")
        f.write("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},\n")
        for cp, b_idx in top_entries:
            f.write(
                f"    {{.bitmap_index = {b_idx}, .adv_w = 320, .box_w = 20, .box_h = 20, .ofs_x = 0, .ofs_y = 0}}, /* U+{cp:04X} */\n"
            )
        f.write("};\n\n")
        f.write("static const lv_font_fmt_txt_cmap_t lut_top_icon_cmaps[] = {\n")
        f.write("    {\n")
        f.write("        .range_start = 0xE008, .range_length = 2, .glyph_id_start = 1,\n")
        f.write("        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0,\n")
        f.write("        .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY\n")
        f.write("    },\n")
        f.write("};\n\n")
        f.write("static const lv_font_fmt_txt_dsc_t lut_top_icon_font_dsc = {\n")
        f.write("    .glyph_bitmap = lut_top_icon_bitmap,\n")
        f.write("    .glyph_dsc = lut_top_icon_glyph_dsc,\n")
        f.write("    .cmaps = lut_top_icon_cmaps,\n")
        f.write("    .kern_dsc = NULL,\n")
        f.write("    .kern_scale = 0,\n")
        f.write("    .cmap_num = 1,\n")
        f.write("    .bpp = 4,\n")
        f.write("    .kern_classes = 0,\n")
        f.write("    .bitmap_format = 0,\n")
        f.write("};\n\n")
        f.write("const lv_font_t zvp_font_lut_top_icons = {\n")
        f.write("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,\n")
        f.write("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n")
        f.write("    .line_height = 20,\n")
        f.write("    .base_line = 0,\n")
        f.write("    .subpx = LV_FONT_SUBPX_NONE,\n")
        f.write("    .underline_position = -1,\n")
        f.write("    .underline_thickness = 1,\n")
        f.write("    .dsc = &lut_top_icon_font_dsc,\n")
        f.write("    .fallback = NULL,\n")
        f.write("};\n")


def main() -> None:
    fp = pick_font_path()
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cps = collect_codepoints(root)
    render_font_c(fp, 24, "zvp_font_zh24", os.path.join(root, "src", "zvp_font_zh24.c"), cps)
    render_font_c(fp, 20, "zvp_font_zh20", os.path.join(root, "src", "zvp_font_zh20.c"), cps)
    zh18_path = os.path.join(root, "src", "zvp_font_zh18.c")
    render_font_c(fp, 18, "zvp_font_zh18", zh18_path, cps)
    append_lut_icons_c(zh18_path)


if __name__ == "__main__":
    main()
