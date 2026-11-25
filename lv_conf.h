/**
 * @file lv_conf.h
 * ZV-E10 LVGL v9.2 极简高透明单色叠加层配置
 */
#if 1

#ifndef LV_CONF_H
#define LV_CONF_H

/* 32-bit ARGB8888 内部渲染：保留完整透明通道，flush 时统一转为 Sony RGBA4444 */
#define LV_COLOR_DEPTH 32

/* 使用 LVGL 内置堆管理/字符串/格式化，避免对相机旧版 glibc 2.11.2 产生高级符号依赖 */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN

#define LV_STDINT_INCLUDE       <stdint.h>
#define LV_STDDEF_INCLUDE       <stddef.h>
#define LV_STDBOOL_INCLUDE      <stdbool.h>
#define LV_INTTYPES_INCLUDE     <inttypes.h>
#define LV_LIMITS_INCLUDE       <limits.h>
#define LV_STDARG_INCLUDE       <stdarg.h>

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    /* 控制在 256 KB 以内，兼顾 640x480 叠加层流畅渲染与 128 MB 系统内存余量 */
    #define LV_MEM_SIZE (256 * 1024U)
    #define LV_MEM_POOL_EXPAND_SIZE 0
    #define LV_MEM_ADR 0
#endif

/* 在 Sony WidgetBase::onAnimate 主 GUI 线程单线程驱动 */
#define LV_DEF_REFR_PERIOD  33
#define LV_DPI_DEF          130
#define LV_USE_OS           LV_OS_NONE

/* 渲染步长对齐 */
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN        4
#define LV_USE_DRAW_SW           1
#define LV_DRAW_SW_SUPPORT_RGB565      1
#define LV_DRAW_SW_SUPPORT_RGB565A8    0
#define LV_DRAW_SW_SUPPORT_RGB888      1
#define LV_DRAW_SW_SUPPORT_XRGB8888    1
#define LV_DRAW_SW_SUPPORT_ARGB8888    1
#define LV_DRAW_SW_SUPPORT_L8          0
#define LV_DRAW_SW_SUPPORT_AL88        0
#define LV_DRAW_SW_SUPPORT_A8          1
#define LV_DRAW_SW_SUPPORT_I1          0
#define LV_DRAW_SW_ASM                 LV_DRAW_SW_ASM_NONE
#define LV_DRAW_SW_COMPLEX             1
#define LV_DRAW_SW_SHADOW_CACHE_SIZE   0
#define LV_DRAW_SW_CIRCLE_CACHE_SIZE   4

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/* 内置基础字体 + 自研精简中文点阵字体 */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_FONT_FMT_TXT_LARGE 0
#define LV_USE_FONT_COMPRESSED 0
#define LV_USE_FONT_PLACEHOLDER 1
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_)]}"
#define LV_USE_BIDI 0

/* 仅启用当前插件 UI 所需的极简组件集 */
#define LV_WIDGETS_HAS_DEFAULT_VALUE 1
#define LV_USE_OBJ_PROPERTY 0
#define LV_USE_ANIMIMG    0
#define LV_USE_ARC        0
#define LV_USE_BAR        0
#define LV_USE_BUTTON     1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CALENDAR   0
#define LV_USE_CANVAS     0
#define LV_USE_CHART      0
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   0
#define LV_USE_IMAGE      1
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD   0
#define LV_USE_LABEL      1
#define LV_USE_LED        0
#define LV_USE_LINE       1
#define LV_USE_LIST       0
#define LV_USE_MENU       0
#define LV_USE_MSGBOX     0
#define LV_USE_ROLLER     0
#define LV_USE_SCALE      0
#define LV_USE_SLIDER     0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    0
#define LV_USE_SWITCH     1
#define LV_USE_TABLE      0
#define LV_USE_TABVIEW    0
#define LV_USE_TEXTAREA   0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0

/* 主题与布局 */
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE  1
#define LV_USE_THEME_MONO    0
#define LV_USE_FLEX          1
#define LV_USE_GRID          0

/* 禁用第三方矢量/文件解码库，最小化依赖与内存占用 */
#define LV_USE_THORVG_INTERNAL 0
#define LV_USE_SYSMON          0
#define LV_USE_PROFILER        0
#define LV_USE_MONKEY          0

#endif /* LV_CONF_H */
#endif /* 1 */
