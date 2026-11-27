#include "zvp_aspect.h"
#include "zvp_abi.h"
#include <string.h>

static const zvp_aspect_preset_t s_aspect_presets[ZVP_ASPECT_PRESET_COUNT] = {
    { "17:6",   "超宽全景 (17:6)",       17u,  6u },
    { "65:24",  "哈苏 XPan (65:24)",     65u, 24u },
    { "2.39:1", "变形宽银幕 (2.39:1)",  239u, 100u },
    { "2.35:1", "电影宽银幕 (2.35:1)",   47u, 20u },
    { "21:9",   "宽屏电影 (21:9 / 7:3)",  7u,  3u },
    { "16:9",   "高清宽屏 (16:9)",       16u,  9u },
    { "3:2",    "经典全画幅 (3:2)",       3u,  2u },
    { "7:6",    "中画幅 6x7 (7:6)",       7u,  6u },
    { "5:4",    "大画幅 4x5 (5:4)",       5u,  4u },
    { "1:1",    "方画幅 (1:1)",           1u,  1u },
    { "4:5",    "竖拍人像 (4:5)",         4u,  5u },
    { "9:16",   "竖屏视频 (9:16)",        9u, 16u }
};

static int16_t clamp_i16(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return (int16_t)lo;
    if (v > hi) return (int16_t)hi;
    return (int16_t)v;
}

static void set_rect(zvp_aspect_rect_t *r, int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (!r) return;
    r->x = (int16_t)x;
    r->y = (int16_t)y;
    r->w = (w > 0) ? (int16_t)w : 0;
    r->h = (h > 0) ? (int16_t)h : 0;
}

void zvp_aspect_init(zvp_aspect_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    zvp_aspect_reset_defaults(ctx);
}

void zvp_aspect_reset_defaults(zvp_aspect_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }
    zvp_aspect_hw_state_t saved_hw = ctx->hw;
    memset(ctx, 0, sizeof(*ctx));
    ctx->hw = saved_hw;
    ctx->enabled = false;
    ctx->preset_idx = 4u; /* 默认选中 21:9 */
    ctx->style = ZVP_ASPECT_STYLE_DIM_45;
    ctx->guide = ZVP_ASPECT_GUIDE_BORDER;
    ctx->par_mode = ZVP_ASPECT_PAR_AUTO;
    zvp_aspect_recompute(ctx, false);
}

const zvp_aspect_preset_t *zvp_aspect_get_presets(size_t *out_count)
{
    if (out_count) {
        *out_count = ZVP_ASPECT_PRESET_COUNT;
    }
    return s_aspect_presets;
}

const zvp_aspect_preset_t *zvp_aspect_get_current_preset(const zvp_aspect_ctx_t *ctx)
{
    uint8_t idx = (ctx && ctx->preset_idx < ZVP_ASPECT_PRESET_COUNT) ? ctx->preset_idx : 0u;
    return &s_aspect_presets[idx];
}

void zvp_aspect_set_enabled(zvp_aspect_ctx_t *ctx, bool enable)
{
    if (ctx) {
        ctx->enabled = enable;
    }
}

void zvp_aspect_select_preset(zvp_aspect_ctx_t *ctx, uint8_t idx)
{
    if (!ctx || idx >= ZVP_ASPECT_PRESET_COUNT) {
        return;
    }
    ctx->preset_idx = idx;
}

void zvp_aspect_cycle_style(zvp_aspect_ctx_t *ctx, int step)
{
    if (!ctx) {
        return;
    }
    int cur = (int)ctx->style;
    int cnt = (int)ZVP_ASPECT_STYLE_COUNT;
    int next = ((cur + step) % cnt + cnt) % cnt;
    ctx->style = (zvp_aspect_style_t)next;
}

void zvp_aspect_cycle_guide(zvp_aspect_ctx_t *ctx, int step)
{
    if (!ctx) {
        return;
    }
    int cur = (int)ctx->guide;
    int cnt = (int)ZVP_ASPECT_GUIDE_COUNT;
    int next = ((cur + step) % cnt + cnt) % cnt;
    ctx->guide = (zvp_aspect_guide_t)next;
}

void zvp_aspect_cycle_par_mode(zvp_aspect_ctx_t *ctx, int step)
{
    if (!ctx) {
        return;
    }
    int cur = (int)ctx->par_mode;
    int cnt = (int)ZVP_ASPECT_PAR_COUNT;
    int next = ((cur + step) % cnt + cnt) % cnt;
    ctx->par_mode = (zvp_aspect_par_mode_t)next;
}

const char *zvp_aspect_style_name(zvp_aspect_style_t style)
{
    switch (style) {
    case ZVP_ASPECT_STYLE_FRAME_ONLY:  return "仅框线";
    case ZVP_ASPECT_STYLE_DIM_45:      return "45%压暗";
    case ZVP_ASPECT_STYLE_DIM_75:      return "75%压暗";
    case ZVP_ASPECT_STYLE_SOLID_BLACK: return "纯黑遮幅";
    default:                           return "45%压暗";
    }
}

uint8_t zvp_aspect_style_opa(zvp_aspect_style_t style)
{
    switch (style) {
    case ZVP_ASPECT_STYLE_FRAME_ONLY:  return 0u;
    case ZVP_ASPECT_STYLE_DIM_45:      return 115u; /* ~45% */
    case ZVP_ASPECT_STYLE_DIM_75:      return 191u; /* ~75% */
    case ZVP_ASPECT_STYLE_SOLID_BLACK: return 255u; /* 100% */
    default:                           return 115u;
    }
}

const char *zvp_aspect_guide_name(zvp_aspect_guide_t guide)
{
    switch (guide) {
    case ZVP_ASPECT_GUIDE_NONE:   return "无框线";
    case ZVP_ASPECT_GUIDE_BORDER: return "构图框线";
    case ZVP_ASPECT_GUIDE_THIRDS: return "框+三分线";
    default:                      return "构图框线";
    }
}

const char *zvp_aspect_par_mode_name(zvp_aspect_par_mode_t mode)
{
    switch (mode) {
    case ZVP_ASPECT_PAR_AUTO: return "自动跟画面";
    case ZVP_ASPECT_PAR_1_1:  return "实拍去底栏";
    case ZVP_ASPECT_PAR_9_8:  return "全屏无底栏";
    case ZVP_ASPECT_PAR_4_3:  return "16:9 画面";
    default:                  return "自动跟画面";
    }
}

bool zvp_aspect_should_display(const zvp_aspect_ctx_t *ctx, bool plugin_menu_visible)
{
    if (!ctx || !ctx->enabled) {
        return false;
    }
    /* 插件菜单打开时保留底层预览方便实时对比切换；关闭菜单后进入原厂 MENU 或回放则自动让位隐藏 */
    if (!plugin_menu_visible && (ctx->hw.camera_in_menu || ctx->hw.camera_in_playback)) {
        return false;
    }
    return true;
}

void zvp_aspect_update_vdf_rect(zvp_aspect_ctx_t *ctx, int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (!ctx) {
        return;
    }
    if (w < 64 || h < 64 || x < 0 || y < 0 ||
        (int32_t)x + (int32_t)w > ZVP_SCREEN_WIDTH ||
        (int32_t)y + (int32_t)h > ZVP_SCREEN_HEIGHT) {
        return;
    }
    ctx->hw.vdf_rect_valid = true;
    ctx->hw.vdf_rect.x = x;
    ctx->hw.vdf_rect.y = y;
    ctx->hw.vdf_rect.w = w;
    ctx->hw.vdf_rect.h = h;
}

void zvp_aspect_update_panel_aspect(zvp_aspect_ctx_t *ctx, bool is_16_9, uint8_t raw0, uint8_t raw1)
{
    if (!ctx) {
        return;
    }
    ctx->hw.panel_aspect_valid = true;
    ctx->hw.panel_is_16_9 = is_16_9;
    ctx->hw.vdf_panel_raw[0] = raw0;
    ctx->hw.vdf_panel_raw[1] = raw1;
}

void zvp_aspect_update_camera_ui_state(zvp_aspect_ctx_t *ctx, bool in_menu, bool in_playback)
{
    if (!ctx) {
        return;
    }
    ctx->hw.camera_in_menu = in_menu;
    ctx->hw.camera_in_playback = in_playback;
}

static void resolve_view_and_par(const zvp_aspect_ctx_t *ctx,
                                 zvp_aspect_rect_t *out_view,
                                 uint16_t *out_num,
                                 uint16_t *out_den)
{
    /* 实机验证 ZV-E10 VdfCh(41) 屏幕与原生 640x428 拍摄画面均为 PAR 1:1 */
    *out_num = 1u;
    *out_den = 1u;

    switch (ctx->par_mode) {
    case ZVP_ASPECT_PAR_1_1:
        /* 强制实拍去底栏 (640x428，顶部 0..427，底下 52px 永远留给拍摄参数) */
        set_rect(out_view, ZVP_LIVEVIEW_DEFAULT_X, ZVP_LIVEVIEW_DEFAULT_Y,
                 ZVP_LIVEVIEW_DEFAULT_W, ZVP_LIVEVIEW_DEFAULT_H);
        break;
    case ZVP_ASPECT_PAR_9_8:
        /* 强制全屏无底栏 (640x480) */
        set_rect(out_view, 0, 0, ZVP_SCREEN_WIDTH, ZVP_SCREEN_HEIGHT);
        break;
    case ZVP_ASPECT_PAR_4_3:
        /* 强制去底栏内的 16:9 拍摄区 (640x360 居于 0..428 正中，即 y=34) */
        set_rect(out_view, 0, 34, ZVP_SCREEN_WIDTH, 360);
        break;
    case ZVP_ASPECT_PAR_AUTO:
    default:
        /* 自动模式：已采集到 VdfCh(41) 非零矩形时直接跟随，否则默认使用去底栏 640x428 */
        if (ctx->hw.vdf_rect_valid) {
            *out_view = ctx->hw.vdf_rect;
        } else {
            set_rect(out_view, ZVP_LIVEVIEW_DEFAULT_X, ZVP_LIVEVIEW_DEFAULT_Y,
                     ZVP_LIVEVIEW_DEFAULT_W, ZVP_LIVEVIEW_DEFAULT_H);
        }
        break;
    }
}

void zvp_aspect_compute_layout(const zvp_aspect_rect_t *view_rect,
                               uint16_t par_num,
                               uint16_t par_den,
                               uint16_t ratio_num,
                               uint16_t ratio_den,
                               bool ui_rotated_180,
                               zvp_aspect_layout_t *out_layout)
{
    (void)ui_rotated_180; /* 180° 翻折屏幕由 zvp_bridge_blit_argb8888 统一整屏翻转，此处不重复倒置 */
    if (!out_layout) {
        return;
    }
    memset(out_layout, 0, sizeof(*out_layout));

    if (par_num == 0u || par_den == 0u) {
        par_num = 1u;
        par_den = 1u;
    }
    if (ratio_num == 0u || ratio_den == 0u) {
        ratio_num = 3u;
        ratio_den = 2u;
    }

    int32_t vx = ZVP_LIVEVIEW_DEFAULT_X;
    int32_t vy = ZVP_LIVEVIEW_DEFAULT_Y;
    int32_t vw = ZVP_LIVEVIEW_DEFAULT_W;
    int32_t vh = ZVP_LIVEVIEW_DEFAULT_H;
    if (view_rect && view_rect->w > 0 && view_rect->h > 0) {
        vx = clamp_i16(view_rect->x, 0, ZVP_SCREEN_WIDTH - 1);
        vy = clamp_i16(view_rect->y, 0, ZVP_SCREEN_HEIGHT - 1);
        vw = clamp_i16(view_rect->w, 1, ZVP_SCREEN_WIDTH - vx);
        vh = clamp_i16(view_rect->h, 1, ZVP_SCREEN_HEIGHT - vy);
    }

    set_rect(&out_layout->view_rect, vx, vy, vw, vh);
    out_layout->par_num = par_num;
    out_layout->par_den = par_den;

    /* 使用 64 位整数防止乘法溢出：
     * 物理有效宽度 ~ vw * par_num，物理有效高度 ~ vh * par_den
     * 比较 (vw * par_num / (vh * par_den)) 与 (ratio_num / ratio_den) */
    uint64_t lhs = (uint64_t)(uint32_t)vw * (uint64_t)par_num * (uint64_t)ratio_den;
    uint64_t rhs = (uint64_t)(uint32_t)vh * (uint64_t)par_den * (uint64_t)ratio_num;

    int32_t fw = vw;
    int32_t fh = vh;
    if (lhs > rhs) {
        /* 原生区域更宽 -> 左右加黑边 (Pillarbox) */
        uint64_t num = (uint64_t)(uint32_t)vh * (uint64_t)par_den * (uint64_t)ratio_num;
        uint64_t den = (uint64_t)par_num * (uint64_t)ratio_den;
        int32_t tw = (int32_t)((num + den / 2u) / den);
        if (tw > vw) tw = vw;
        if (tw < 2) tw = 2;
        /* 对齐至 2 的倍数确保左右居中对称误差 <= 1px */
        tw &= ~1;
        if (tw < 2) tw = 2;
        fw = tw;
    } else if (lhs < rhs) {
        /* 原生区域更高 -> 上下加黑边 (Letterbox) */
        uint64_t num = (uint64_t)(uint32_t)vw * (uint64_t)par_num * (uint64_t)ratio_den;
        uint64_t den = (uint64_t)par_den * (uint64_t)ratio_num;
        int32_t th = (int32_t)((num + den / 2u) / den);
        if (th > vh) th = vh;
        if (th < 2) th = 2;
        th &= ~1;
        if (th < 2) th = 2;
        fh = th;
    }

    int32_t bar_l = (vw - fw) / 2;
    int32_t bar_r = vw - fw - bar_l;
    int32_t bar_t = (vh - fh) / 2;
    int32_t bar_b = vh - fh - bar_t;

    set_rect(&out_layout->frame_rect, vx + bar_l, vy + bar_t, fw, fh);
    set_rect(&out_layout->bar_top,    vx, vy, vw, bar_t);
    set_rect(&out_layout->bar_bottom, vx, vy + bar_t + fh, vw, bar_b);
    set_rect(&out_layout->bar_left,   vx, vy + bar_t, bar_l, fh);
    set_rect(&out_layout->bar_right,  vx + bar_l + fw, vy + bar_t, bar_r, fh);
    out_layout->valid = true;
}

void zvp_aspect_recompute(zvp_aspect_ctx_t *ctx, bool ui_rotated_180)
{
    if (!ctx) {
        return;
    }
    const zvp_aspect_preset_t *p = zvp_aspect_get_current_preset(ctx);
    uint16_t par_num = 1u;
    uint16_t par_den = 1u;
    zvp_aspect_rect_t vrect;
    resolve_view_and_par(ctx, &vrect, &par_num, &par_den);

    zvp_aspect_compute_layout(&vrect,
                              par_num,
                              par_den,
                              p->ratio_num,
                              p->ratio_den,
                              ui_rotated_180,
                              &ctx->layout);
}
