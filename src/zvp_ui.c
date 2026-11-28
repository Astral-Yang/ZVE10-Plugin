#include "zvp_runtime.h"
#include <stdio.h>
#include <string.h>

/* 在 src/zvp_font_zh24.c / zh20.c / zh18.c 中导出的全字符集抗锯齿中文字体及 LUT 图标字体 */
extern const lv_font_t zvp_font_zh24;
extern const lv_font_t zvp_font_zh20;
extern const lv_font_t zvp_font_zh18;
extern const lv_font_t zvp_font_lut_icons;
extern const lv_font_t zvp_font_lut_top_icons;

/* 单色极简高透明浮窗视觉常量 */
#define ZVP_COLOR_BG_BLACK     lv_color_hex(0x000000)
#define ZVP_COLOR_WHITE        lv_color_hex(0xFFFFFF)
#define ZVP_COLOR_TEXT_SUB     lv_color_hex(0xDCDCDC)
#define ZVP_OPA_PANEL_BG       122 /* rgba(0,0,0,0.48) */
#define ZVP_OPA_CARD_BG        86  /* rgba(0,0,0,0.34) */
#define ZVP_OPA_BTN_ACTIVE     88
#define ZVP_OPA_BTN_IDLE       36
#define ZVP_OPA_BORDER_NORM    110
#define ZVP_OPA_BORDER_FOCUS   255

static void style_glass_panel(lv_obj_t *obj, int32_t radius)
{
    lv_obj_set_style_bg_color(obj, ZVP_COLOR_BG_BLACK, 0);
    lv_obj_set_style_bg_opa(obj, ZVP_OPA_PANEL_BG, 0);
    lv_obj_set_style_border_color(obj, ZVP_COLOR_WHITE, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_opa(obj, ZVP_OPA_BORDER_NORM, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
}

static void style_mono_btn(lv_obj_t *btn)
{
    lv_obj_set_style_bg_color(btn, ZVP_COLOR_WHITE, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, ZVP_OPA_BTN_IDLE, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, ZVP_COLOR_WHITE, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(btn, ZVP_OPA_BORDER_NORM, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 8, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_STATE_DEFAULT);

    /* 按下瞬时反馈（防止 theme_simple 残留灰色覆盖） */
    lv_obj_set_style_bg_color(btn, ZVP_COLOR_WHITE, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, ZVP_OPA_BTN_ACTIVE, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, ZVP_COLOR_WHITE, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 2, LV_STATE_PRESSED);

    /* 焦点高对比纯白 3px 粗框（不改动背景透明度，消除先灰后白误解） */
    lv_obj_set_style_border_color(btn, ZVP_COLOR_WHITE, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(btn, 3, LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(btn, ZVP_OPA_BORDER_FOCUS, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(btn, ZVP_COLOR_WHITE, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(btn, 3, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_opa(btn, ZVP_OPA_BORDER_FOCUS, LV_STATE_FOCUS_KEY);

    /* 激活/选中反色高对比 */
    lv_obj_set_style_bg_color(btn, ZVP_COLOR_WHITE, LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btn, ZVP_COLOR_BG_BLACK, LV_STATE_CHECKED);
}

static void style_action_card(lv_obj_t *card)
{
    lv_obj_set_style_bg_color(card, ZVP_COLOR_BG_BLACK, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(card, ZVP_OPA_CARD_BG, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(card, ZVP_COLOR_WHITE, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(card, 90, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(card, 8, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(card, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_hor(card, 14, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(card, 10, LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(card, ZVP_COLOR_BG_BLACK, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(card, 128, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(card, ZVP_COLOR_WHITE, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(card, 2, LV_STATE_PRESSED);

    /* 焦点仅保留纯白 3px 边框 */
    lv_obj_set_style_border_color(card, ZVP_COLOR_WHITE, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(card, 3, LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(card, ZVP_OPA_BORDER_FOCUS, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(card, ZVP_COLOR_WHITE, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(card, 3, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_opa(card, ZVP_OPA_BORDER_FOCUS, LV_STATE_FOCUS_KEY);
}

static void style_fine_tile(lv_obj_t *obj)
{
    /* 独立浮块内部采用一/二级面板(48%黑)+按钮(14%白)复合等价背景 rgba(65,65,65,0.55)，无整体大外框 */
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x414141), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, 141, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, ZVP_COLOR_WHITE, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(obj, ZVP_OPA_BORDER_NORM, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(obj, 6, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(obj, 0, LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(obj, ZVP_COLOR_WHITE, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, ZVP_OPA_BTN_ACTIVE, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(obj, ZVP_COLOR_WHITE, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(obj, 2, LV_STATE_PRESSED);

    lv_obj_set_style_border_color(obj, ZVP_COLOR_WHITE, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(obj, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(obj, ZVP_OPA_BORDER_FOCUS, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(obj, ZVP_COLOR_WHITE, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(obj, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_opa(obj, ZVP_OPA_BORDER_FOCUS, LV_STATE_FOCUS_KEY);
}

static lv_obj_t *create_card(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    style_action_card(card);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
    return card;
}

static lv_obj_t *create_click_card(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *card = lv_button_create(parent);
    lv_obj_set_size(card, w, h);
    style_action_card(card);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    return lbl;
}

static lv_obj_t *make_state_badge(lv_obj_t *parent, int32_t w, int32_t h, const char *init_txt, lv_obj_t **out_lbl)
{
    lv_obj_t *badge = lv_obj_create(parent);
    lv_obj_set_size(badge, w, h);
    style_mono_btn(badge);
    lv_obj_set_scrollbar_mode(badge, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lbl = make_label(badge, init_txt, &zvp_font_zh24, ZVP_COLOR_WHITE);
    lv_obj_center(lbl);
    if (out_lbl) {
        *out_lbl = lbl;
    }
    return badge;
}

static void set_badge_checked(lv_obj_t *badge, lv_obj_t *lbl, bool checked, const char *txt)
{
    if (!badge || !lbl) {
        return;
    }
    if (checked) {
        lv_obj_add_state(badge, LV_STATE_CHECKED);
        lv_obj_set_style_text_color(lbl, ZVP_COLOR_BG_BLACK, 0);
    } else {
        lv_obj_clear_state(badge, LV_STATE_CHECKED);
        lv_obj_set_style_text_color(lbl, ZVP_COLOR_WHITE, 0);
    }
    if (txt) {
        lv_label_set_text(lbl, txt);
    }
}

static void set_focus_obj(zvp_ui_t *ui, lv_obj_t *target)
{
    if (!ui || !ui->built) {
        return;
    }
    if (ui->focused_obj && ui->focused_obj != target) {
        lv_obj_clear_state(ui->focused_obj, LV_STATE_FOCUSED);
    }
    ui->focused_obj = target;
    if (target) {
        lv_obj_add_state(target, LV_STATE_FOCUSED);
        if (ui->nav_list && lv_obj_get_parent(target) == ui->nav_list) {
            lv_obj_scroll_to_view(target, LV_ANIM_OFF);
        }
        if (ui->lut_list && lv_obj_get_parent(target) == ui->lut_list) {
            lv_obj_scroll_to_view(target, LV_ANIM_OFF);
        }
        if (ui->input_group && lv_obj_get_group(target) == ui->input_group) {
            lv_group_focus_obj(target);
        }
    }
}

/* ============================================================================
 * UI 事件响应回调（触控与按键共用）
 * ========================================================================== */

static void trigger_wifi5g_toggle(zvp_runtime_t *rt, const char *src)
{
    if (!rt) return;
    bool next = !rt->wifi.enabled;
    int rc = zvp_wifi5g_set_enabled(&rt->wifi, next);
    zvp_runtime_save_prefs();
    zvp_logf("ui wifi5g toggle -> next=%d rc=%d state=%d src=%s (tp=(%d,%d) key=0x%x)",
             next ? 1 : 0, rc, (int)rt->wifi.state, src ? src : "?",
             (int)rt->diag_last_tp_x, (int)rt->diag_last_tp_y, (unsigned)rt->diag_last_key_w0);
    zvp_ui_refresh(&rt->ui);
}

static void trigger_channel_select(zvp_runtime_t *rt, uint8_t ch, const char *src)
{
    if (!rt) return;
    if (!rt->wifi.enabled) {
        zvp_wifi5g_set_enabled(&rt->wifi, true);
    }
    int rc = zvp_wifi5g_select_channel(&rt->wifi, ch);
    zvp_runtime_save_prefs();
    zvp_logf("ui channel select -> ch=%u rc=%d state=%d src=%s (tp=(%d,%d) key=0x%x)",
             (unsigned)ch, rc, (int)rt->wifi.state, src ? src : "?",
             (int)rt->diag_last_tp_x, (int)rt->diag_last_tp_y, (unsigned)rt->diag_last_key_w0);
    zvp_ui_refresh(&rt->ui);
}

static void trigger_c1_mode_toggle(zvp_runtime_t *rt, const char *src)
{
    if (!rt) return;
    zvp_c1_mode_t next = (rt->input.c1_mode == ZVP_C1_MODE_LONG_PRESS)
        ? ZVP_C1_MODE_SHORT_EXCLUSIVE
        : ZVP_C1_MODE_LONG_PRESS;
    zvp_input_set_c1_mode(&rt->input, next);
    zvp_runtime_save_prefs();
    zvp_logf("ui c1 mode -> %d src=%s (tp=(%d,%d) key=0x%x)",
             (int)next, src ? src : "?",
             (int)rt->diag_last_tp_x, (int)rt->diag_last_tp_y, (unsigned)rt->diag_last_key_w0);
    zvp_ui_refresh(&rt->ui);
}

static void trigger_auto_flip_toggle(zvp_runtime_t *rt, const char *src)
{
    if (!rt) return;
    zvp_input_set_auto_flip(&rt->input, !rt->input.auto_flip);
    zvp_runtime_save_prefs();
    zvp_logf("ui auto_flip -> %d src=%s (tp=(%d,%d) key=0x%x)",
             rt->input.auto_flip ? 1 : 0, src ? src : "?",
             (int)rt->diag_last_tp_x, (int)rt->diag_last_tp_y, (unsigned)rt->diag_last_key_w0);
    zvp_ui_refresh(&rt->ui);
}

static void trigger_native_touch_toggle(zvp_runtime_t *rt, const char *src)
{
    if (!rt) return;
    bool next = !rt->native_menu_touch_enabled;
    rt->native_menu_touch_enabled = next;
    if (next) {
        zvp_menu_touch_stats_t st;
        zvp_menu_touch_get_stats(&st);
        if (!st.hooks_installed) {
            zvp_menu_touch_ops_t mops;
            if (zvp_bridge_init_menu_touch_ops(&mops) == 0) {
                (void)zvp_menu_touch_init(&mops);
            }
        }
    }
    zvp_menu_touch_set_enabled(next);
    zvp_runtime_save_prefs();
    zvp_logf("ui native_menu_touch -> %d src=%s", next ? 1 : 0, src ? src : "?");
    zvp_ui_refresh(&rt->ui);
}

static void trigger_recovery(zvp_runtime_t *rt, const char *src)
{
    if (!rt) return;
    zvp_recovery_phase_t ph = zvp_recovery_trigger(&rt->rec, &rt->wifi, zvp_monotonic_ms());
    zvp_logf("ui recovery -> phase=%d src=%s (tp=(%d,%d) key=0x%x)",
             (int)ph, src ? src : "?",
             (int)rt->diag_last_tp_x, (int)rt->diag_last_tp_y, (unsigned)rt->diag_last_key_w0);
    zvp_ui_sync_overlay(&rt->ui);
    zvp_ui_refresh(&rt->ui);
}

static void trigger_shutter_sound_toggle(zvp_runtime_t *rt, const char *src)
{
    if (!rt) return;
    rt->shutter_sound_enabled = !rt->shutter_sound_enabled;
    if (rt->shutter_sound_enabled) {
        (void)zvp_sound_prepare_current(&rt->sound);
    }
    zvp_runtime_save_prefs();
    zvp_logf("ui shutter sound -> %d src=%s",
             rt->shutter_sound_enabled ? 1 : 0, src ? src : "?");
    zvp_ui_refresh(&rt->ui);
    zvp_ui_sync_overlay(&rt->ui);
}

static void trigger_mini_widget_toggle(zvp_runtime_t *rt, const char *src)
{
    if (!rt) return;
    rt->mini_widget_enabled = !rt->mini_widget_enabled;
    zvp_runtime_save_prefs();
    zvp_logf("ui mini widget -> %d src=%s",
             rt->mini_widget_enabled ? 1 : 0, src ? src : "?");
    zvp_ui_refresh(&rt->ui);
    zvp_ui_sync_overlay(&rt->ui);
}

static void trigger_sound_step(zvp_runtime_t *rt, int delta, const char *src)
{
    if (!rt) return;
    if (delta < 0) {
        zvp_sound_prev(&rt->sound);
    } else {
        zvp_sound_next(&rt->sound);
    }
    zvp_runtime_save_prefs();
    zvp_logf("ui sound select -> idx=%u name=%s rc=%d src=%s",
             (unsigned)rt->sound.selected_idx, rt->sound.selected_name,
             (int)rt->sound.last_error, src ? src : "?");
    zvp_ui_refresh(&rt->ui);
}

static void trigger_sound_play(zvp_runtime_t *rt, const char *src)
{
    if (!rt) return;
    bool ok = zvp_sound_play(&rt->sound, zvp_monotonic_ms());
    zvp_logf("ui sound play -> ok=%d slot=0x%02x bytes=%u cnt=%u err=%d src=%s",
             ok ? 1 : 0, (unsigned)rt->sound.active_slot,
             (unsigned)rt->sound.last_f0_bytes,
             (unsigned)rt->sound.play_count,
             (int)rt->sound.last_error,
             src ? src : "?");
    zvp_ui_refresh(&rt->ui);
}

static void trigger_aspect_toggle(zvp_runtime_t *rt, const char *src)
{
    if (!rt) return;
    zvp_aspect_set_enabled(&rt->aspect, !rt->aspect.enabled);
    zvp_aspect_recompute(&rt->aspect, zvp_input_is_ui_rotated_180(&rt->input));
    zvp_runtime_save_prefs();
    zvp_logf("ui aspect toggle -> en=%d preset=%s frame=(%d,%d,%d,%d) src=%s",
             rt->aspect.enabled ? 1 : 0,
             zvp_aspect_get_current_preset(&rt->aspect)->label,
             (int)rt->aspect.layout.frame_rect.x, (int)rt->aspect.layout.frame_rect.y,
             (int)rt->aspect.layout.frame_rect.w, (int)rt->aspect.layout.frame_rect.h,
             src ? src : "?");
    zvp_ui_refresh(&rt->ui);
    zvp_ui_sync_overlay(&rt->ui);
}

static void trigger_aspect_preset(zvp_runtime_t *rt, uint8_t idx, const char *src)
{
    if (!rt) return;
    if (!rt->aspect.enabled) {
        zvp_aspect_set_enabled(&rt->aspect, true);
    }
    zvp_aspect_select_preset(&rt->aspect, idx);
    zvp_aspect_recompute(&rt->aspect, zvp_input_is_ui_rotated_180(&rt->input));
    zvp_runtime_save_prefs();
    zvp_logf("ui aspect preset -> %s frame=(%d,%d,%d,%d) par=%u:%u src=%s",
             zvp_aspect_get_current_preset(&rt->aspect)->label,
             (int)rt->aspect.layout.frame_rect.x, (int)rt->aspect.layout.frame_rect.y,
             (int)rt->aspect.layout.frame_rect.w, (int)rt->aspect.layout.frame_rect.h,
             (unsigned)rt->aspect.layout.par_num, (unsigned)rt->aspect.layout.par_den,
             src ? src : "?");
    zvp_ui_refresh(&rt->ui);
    zvp_ui_sync_overlay(&rt->ui);
}

static void trigger_aspect_style_cycle(zvp_runtime_t *rt, int step, const char *src)
{
    if (!rt) return;
    zvp_aspect_cycle_style(&rt->aspect, step);
    zvp_runtime_save_prefs();
    zvp_logf("ui aspect style -> %s src=%s",
             zvp_aspect_style_name(rt->aspect.style), src ? src : "?");
    zvp_ui_refresh(&rt->ui);
    zvp_ui_sync_overlay(&rt->ui);
}

static void trigger_aspect_guide_cycle(zvp_runtime_t *rt, int step, const char *src)
{
    if (!rt) return;
    zvp_aspect_cycle_guide(&rt->aspect, step);
    zvp_runtime_save_prefs();
    zvp_logf("ui aspect guide -> %s src=%s",
             zvp_aspect_guide_name(rt->aspect.guide), src ? src : "?");
    zvp_ui_refresh(&rt->ui);
    zvp_ui_sync_overlay(&rt->ui);
}

static void trigger_aspect_par_cycle(zvp_runtime_t *rt, int step, const char *src)
{
    if (!rt) return;
    zvp_aspect_cycle_par_mode(&rt->aspect, step);
    zvp_aspect_recompute(&rt->aspect, zvp_input_is_ui_rotated_180(&rt->input));
    zvp_runtime_save_prefs();
    zvp_logf("ui aspect base -> %s view=(%d,%d,%d,%d) frame=(%d,%d,%d,%d) src=%s",
             zvp_aspect_par_mode_name(rt->aspect.par_mode),
             (int)rt->aspect.layout.view_rect.x, (int)rt->aspect.layout.view_rect.y,
             (int)rt->aspect.layout.view_rect.w, (int)rt->aspect.layout.view_rect.h,
             (int)rt->aspect.layout.frame_rect.x, (int)rt->aspect.layout.frame_rect.y,
             (int)rt->aspect.layout.frame_rect.w, (int)rt->aspect.layout.frame_rect.h,
             src ? src : "?");
    zvp_ui_refresh(&rt->ui);
    zvp_ui_sync_overlay(&rt->ui);
}

static void on_aspect_focus_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, lv_event_get_target_obj(e));
}

static void on_aspect_badge_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, rt->ui.btn_aspect_enable);
    trigger_aspect_toggle(rt, "touch_badge");
}

static void on_aspect_preset_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, lv_event_get_target_obj(e));
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    trigger_aspect_preset(rt, (uint8_t)idx, "touch");
}

static void on_aspect_style_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, rt->ui.btn_aspect_style);
    trigger_aspect_style_cycle(rt, 1, "touch");
}

static void on_aspect_guide_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, rt->ui.btn_aspect_guide);
    trigger_aspect_guide_cycle(rt, 1, "touch");
}

static void on_aspect_par_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, rt->ui.btn_aspect_par);
    trigger_aspect_par_cycle(rt, 1, "touch");
}

/* ---------- 屏幕校色页事件回调 ---------- */
static void trigger_cal_toggle_enable(zvp_runtime_t *rt, const char *src)
{
    if (!rt) return;
    bool next = !rt->color_cal.enabled;
    int rc = zvp_color_cal_set_enabled(&rt->color_cal, next, true, zvp_monotonic_ms());
    zvp_logf("[cal] toggle(%s) -> en=%d rc=%d msg=%s",
             src ? src : "?", next ? 1 : 0, rc, rt->color_cal.last_msg);
    zvp_ui_refresh(&rt->ui);
}

static void trigger_cal_toggle_compare(zvp_runtime_t *rt, const char *src)
{
    if (!rt) return;
    int rc = zvp_color_cal_toggle_compare(&rt->color_cal, zvp_monotonic_ms());
    zvp_logf("[cal] compare(%s) -> en=%d cmp=%d rc=%d",
             src ? src : "?",
             rt->color_cal.enabled ? 1 : 0,
             rt->color_cal.compare_original ? 1 : 0,
             rc);
    zvp_ui_refresh(&rt->ui);
}

static void trigger_cal_cycle_preset(zvp_runtime_t *rt, int step, const char *src)
{
    if (!rt) return;
    int cnt = (int)ZVP_CAL_PRESET_COUNT;
    int cur = (int)rt->color_cal.preset_idx;
    int next = ((cur + step) % cnt + cnt) % cnt;
    int rc = zvp_color_cal_select_preset(&rt->color_cal, (uint8_t)next, true, zvp_monotonic_ms());
    zvp_logf("[cal] preset(%s) -> idx=%d (%s) rc=%d",
             src ? src : "?", next, zvp_cal_get_preset((uint8_t)next)->name, rc);
    zvp_ui_refresh(&rt->ui);
}

static void trigger_cal_step_param(zvp_runtime_t *rt, int delta, const char *src)
{
    if (!rt) return;
    if ((uint32_t)rt->ui.cal_sel_param >= ZVP_CAL_PARAM_COUNT) {
        rt->ui.cal_sel_param = ZVP_CAL_PARAM_TEMP;
    }
    int rc = zvp_color_cal_step_param(&rt->color_cal, rt->ui.cal_sel_param, delta, zvp_monotonic_ms());
    zvp_logf("[cal] step(%s) p=%u delta=%d val=%d rc=%d",
             src ? src : "?",
             (unsigned)rt->ui.cal_sel_param, delta,
             (int)rt->color_cal.params.vals[rt->ui.cal_sel_param], rc);
    zvp_ui_refresh(&rt->ui);
}

static void trigger_cal_reset(zvp_runtime_t *rt, const char *src)
{
    if (!rt) return;
    int rc = zvp_color_cal_reset_params(&rt->color_cal, true, zvp_monotonic_ms());
    zvp_logf("[cal] reset(%s) rc=%d", src ? src : "?", rc);
    zvp_ui_refresh(&rt->ui);
}

static void on_cal_enable_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, rt->ui.btn_cal_enable);
    trigger_cal_toggle_enable(rt, "touch");
}

static void on_cal_compare_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, rt->ui.btn_cal_compare);
    trigger_cal_toggle_compare(rt, "touch");
}

static void on_cal_preset_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, rt->ui.btn_cal_preset);
    trigger_cal_cycle_preset(rt, 1, "touch");
}

static void on_cal_reset_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, rt->ui.btn_cal_reset);
    trigger_cal_reset(rt, "touch");
}

static void on_cal_minus_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    trigger_cal_step_param(rt, -1, "touch_minus");
}

static void on_cal_plus_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    trigger_cal_step_param(rt, 1, "touch_plus");
}

static void on_cal_param_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (idx >= ZVP_CAL_PARAM_COUNT) return;
    rt->ui.cal_sel_param = (zvp_cal_param_t)idx;
    if (rt->ui.cal_param_btns[idx]) {
        set_focus_obj(&rt->ui, rt->ui.cal_param_btns[idx]);
    }
    zvp_ui_refresh(&rt->ui);
}

static void on_cal_fine_reset_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    if (rt->ui.btn_cal_fine_reset) {
        set_focus_obj(&rt->ui, rt->ui.btn_cal_fine_reset);
    }
    trigger_cal_reset(rt, "touch_fine_reset");
}

static uint8_t lut_slot_category(const zvp_lut_ctx_t *lut, uint32_t slot)
{
    if (!lut || slot == 0u || slot >= lut->num_slots) {
        return ZVP_LUT_CAT_SONY;
    }
    const char *id = lut->look_ids[slot];
    if (strncmp(id, "fuji-", 5) == 0) {
        return ZVP_LUT_CAT_FUJI;
    }
    if (strncmp(id, "ricoh-", 6) == 0) {
        return ZVP_LUT_CAT_RICOH;
    }
    if (strncmp(id, "zv1m2-", 6) == 0 || strncmp(id, "sony-", 5) == 0) {
        return ZVP_LUT_CAT_SONY;
    }
    return ZVP_LUT_CAT_RICOH;
}

static void lut_rebuild_visible_rows(zvp_ui_t *ui, const zvp_lut_ctx_t *lut)
{
    if (!ui || !lut) return;
    uint32_t nslots = (lut->num_slots > 0u && lut->num_slots <= ZVP_LUT_NUM_SLOTS)
                          ? lut->num_slots
                          : 1u;
    if (ui->lut_cat >= ZVP_LUT_CAT_COUNT) {
        ui->lut_cat = ZVP_LUT_CAT_SONY;
    }
    uint8_t cnt = 0u;
    for (uint32_t s = 1u; s < nslots && cnt < ZVP_LUT_NUM_SLOTS; s++) {
        if (lut_slot_category(lut, s) == ui->lut_cat) {
            ui->lut_row_slots[cnt++] = (uint8_t)s;
        }
    }
    /* 若当前分类暂无条目（如后续自定义包），兜底展示全部可用 LUT 槽位 */
    if (cnt == 0u) {
        for (uint32_t s = 1u; s < nslots && cnt < ZVP_LUT_NUM_SLOTS; s++) {
            ui->lut_row_slots[cnt++] = (uint8_t)s;
        }
    }
    ui->lut_visible_count = cnt;
}

static int lut_find_row_for_slot(const zvp_ui_t *ui, uint32_t slot)
{
    if (!ui || slot == 0u) return -1;
    for (uint32_t r = 0u; r < ui->lut_visible_count; r++) {
        if (ui->lut_row_slots[r] == slot) {
            return (int)r;
        }
    }
    return -1;
}

static void lut_focus_slot_or_cat(zvp_ui_t *ui, const zvp_lut_ctx_t *lut)
{
    if (!ui || !lut) return;
    if (lut->active_sel > 0u && lut->active_sel < lut->num_slots) {
        ui->lut_cat = lut_slot_category(lut, lut->active_sel);
        lut_rebuild_visible_rows(ui, lut);
        int row = lut_find_row_for_slot(ui, lut->active_sel);
        if (row >= 0 && ui->lut_row_btns[row]) {
            set_focus_obj(ui, ui->lut_row_btns[row]);
            return;
        }
    }
    lut_rebuild_visible_rows(ui, lut);
    if (ui->lut_cat_btns[ui->lut_cat]) {
        set_focus_obj(ui, ui->lut_cat_btns[ui->lut_cat]);
    }
}

static void trigger_lut_preview_slot(zvp_runtime_t *rt, uint32_t sel, bool flush_now, const char *src)
{
    if (!rt || sel >= rt->lut.num_slots) return;
    int rc = zvp_lut_select_slot(&rt->lut, sel, flush_now, true);
    if (sel > 0u) {
        rt->lut_last_nonzero_sel = sel;
    }
    rt->lut_cfg_dirty_ms = flush_now ? 0u : zvp_monotonic_ms();
    zvp_logf("[lut] ui preview slot=%u (%s) rc=%d kmod=%d view=%d flush=%d src=%s",
             (unsigned)sel, rt->lut.look_names[sel], rc,
             rt->lut.kmod_loaded ? 1 : 0, rt->lut.last_preview.rc,
             flush_now ? 1 : 0, src ? src : "?");
    zvp_ui_sync_overlay(&rt->ui);
}

static void trigger_lut_fine_step(zvp_runtime_t *rt, int delta, const char *src)
{
    if (!rt || rt->lut.active_sel == 0u || rt->lut.active_sel >= rt->lut.num_slots) return;
    int rc = zvp_lut_adjust_param_step(&rt->lut, rt->ui.lut_fine_param, delta);
    rt->lut_cfg_dirty_ms = zvp_monotonic_ms();
    rt->lut.cfg_dirty = true;
    zvp_logf("[lut] fine step slot=%u param=%d delta=%d rc=%d view=%d src=%s",
             (unsigned)rt->lut.active_sel, (int)rt->ui.lut_fine_param, delta, rc,
             rt->lut.last_preview.rc, src ? src : "?");
    zvp_ui_refresh(&rt->ui);
}

static void trigger_lut_fine_reset(zvp_runtime_t *rt, const char *src)
{
    if (!rt || rt->lut.active_sel == 0u || rt->lut.active_sel >= rt->lut.num_slots) return;
    int rc = zvp_lut_reset_current_adjust(&rt->lut, true);
    rt->lut_cfg_dirty_ms = 0u;
    zvp_logf("[lut] fine reset slot=%u rc=%d src=%s", (unsigned)rt->lut.active_sel, rc, src ? src : "?");
    zvp_ui_refresh(&rt->ui);
}

static uint32_t lut_sd_visible_rows(const zvp_ui_t *ui)
{
    if (!ui || !ui->lut_sd_mode) return 0u;
    if (!ui->lut_sd_has_card_files) {
        return (ui->lut_sd_count > 0u) ? (uint32_t)(ui->lut_sd_count + 1u) : 1u;
    }
    return ui->lut_sd_count;
}

static void trigger_lut_enter_sd_mode(zvp_runtime_t *rt, const char *src)
{
    if (!rt) return;
    zvp_ui_t *ui = &rt->ui;
    uint32_t cnt = 0u;
    int rc = zvp_lut_scan_sd_bundles(&rt->lut, ui->lut_sd_items, ZVP_LUT_SD_MAX_ITEMS, &cnt);
    ui->lut_sd_count = (uint8_t)((cnt <= ZVP_LUT_SD_MAX_ITEMS) ? cnt : ZVP_LUT_SD_MAX_ITEMS);
    ui->lut_sd_has_card_files = false;
    for (uint32_t i = 0u; i < ui->lut_sd_count; i++) {
        if (!ui->lut_sd_items[i].is_embedded) {
            ui->lut_sd_has_card_files = true;
            break;
        }
    }
    ui->lut_sd_mode = true;
    zvp_logf("[lut] 进入SD导入模式 src=%s rc=%d total=%u has_sd=%d",
             src ? src : "?", rc, (unsigned)ui->lut_sd_count, ui->lut_sd_has_card_files ? 1 : 0);
    zvp_ui_sync_overlay(ui);
    if (lut_sd_visible_rows(ui) > 0u && ui->lut_row_btns[0]) {
        set_focus_obj(ui, ui->lut_row_btns[0]);
    }
}

static void trigger_lut_sd_row_activate(zvp_runtime_t *rt, uint32_t row_idx, const char *src)
{
    if (!rt) return;
    zvp_ui_t *ui = &rt->ui;
    if (!ui->lut_sd_has_card_files && row_idx == 0u) {
        trigger_lut_enter_sd_mode(rt, "rescan");
        return;
    }
    uint32_t item_idx = ui->lut_sd_has_card_files ? row_idx : (row_idx - 1u);
    if (item_idx >= ui->lut_sd_count) return;
    const zvp_lut_sd_item_t *it = &ui->lut_sd_items[item_idx];
    int rc = zvp_lut_hot_import_item(&rt->lut, it);
    zvp_logf("[lut] 热导入(%s, emb=%d) src=%s -> rc=%d msg=%s",
             it->name, it->is_embedded ? 1 : 0, src ? src : "?", rc, rt->lut.last_msg);
    if (rc == 0) {
        ui->lut_sd_mode = false;
        lut_focus_slot_or_cat(ui, &rt->lut);
        zvp_ui_sync_overlay(ui);
    } else {
        zvp_ui_refresh(ui);
    }
}

static void on_lut_off_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    rt->ui.lut_sd_mode = false;
    set_focus_obj(&rt->ui, rt->ui.btn_lut_off);
    trigger_lut_preview_slot(rt, 0u, true, "touch_off");
    zvp_ui_refresh(&rt->ui);
}

static void on_lut_cat_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    uint32_t c = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (c >= ZVP_LUT_CAT_COUNT) return;
    rt->ui.lut_sd_mode = false;
    rt->ui.lut_cat = (uint8_t)c;
    lut_rebuild_visible_rows(&rt->ui, &rt->lut);
    set_focus_obj(&rt->ui, rt->ui.lut_cat_btns[c]);
    zvp_ui_sync_overlay(&rt->ui);
}

static void on_lut_row_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    uint32_t row_idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (rt->ui.lut_sd_mode) {
        if (row_idx < lut_sd_visible_rows(&rt->ui)) {
            set_focus_obj(&rt->ui, rt->ui.lut_row_btns[row_idx]);
            trigger_lut_sd_row_activate(rt, row_idx, "touch_sd");
        }
        return;
    }
    if (row_idx >= rt->ui.lut_visible_count) return;
    uint32_t sel = rt->ui.lut_row_slots[row_idx];
    if (sel == 0u || sel >= rt->lut.num_slots) return;
    set_focus_obj(&rt->ui, rt->ui.lut_row_btns[row_idx]);
    if (rt->lut.active_sel == sel) {
        /* 已高亮当前 LUT 时再点，直接聚焦到右下微调第一项 */
        if ((uint32_t)rt->ui.lut_fine_param >= ZVP_LUT_PARAM_COUNT) {
            rt->ui.lut_fine_param = ZVP_LUT_PARAM_STRENGTH;
        }
        set_focus_obj(&rt->ui, rt->ui.lut_param_btns[rt->ui.lut_fine_param]);
        zvp_ui_refresh(&rt->ui);
    } else {
        trigger_lut_preview_slot(rt, sel, true, "touch_row");
    }
}

static void on_lut_sd_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, rt->ui.btn_lut_sd);
    trigger_lut_enter_sd_mode(rt, "touch_sd_btn");
}

static void on_lut_param_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    zvp_lut_param_t p = (zvp_lut_param_t)(uintptr_t)lv_event_get_user_data(e);
    if (p >= ZVP_LUT_PARAM_COUNT) return;

    lv_obj_t *tile = rt->ui.lut_param_btns[p];
    bool was_focused = (rt->ui.focused_obj == tile);
    rt->ui.lut_fine_param = p;
    set_focus_obj(&rt->ui, tile);

    if (was_focused && tile) {
        /* 单行极简触控：已聚焦参数再点上半区 +1，点下半区 -1 */
        lv_indev_t *indev = lv_indev_active();
        if (indev) {
            lv_point_t pt;
            lv_area_t area;
            lv_indev_get_point(indev, &pt);
            lv_obj_get_coords(tile, &area);
            int32_t mid_y = (area.y1 + area.y2) / 2;
            trigger_lut_fine_step(rt, (pt.y <= mid_y) ? 1 : -1, "touch_tile");
            return;
        }
    }
    zvp_ui_refresh(&rt->ui);
}

static void on_lut_fine_reset_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, rt->ui.btn_lut_fine_reset);
    trigger_lut_fine_reset(rt, "touch_reset");
}

static void on_shutter_focus_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, lv_event_get_target_obj(e));
}

static void on_shutter_badge_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, rt->ui.btn_shutter_enable);
    trigger_shutter_sound_toggle(rt, "touch_badge");
}

static void on_mini_focus_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, lv_event_get_target_obj(e));
}

static void on_mini_badge_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, rt->ui.btn_mini_enable);
    trigger_mini_widget_toggle(rt, "touch_badge");
}

static void on_sound_prev_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    if (rt->input.menu_visible) {
        set_focus_obj(&rt->ui, rt->ui.btn_sound_prev);
        trigger_sound_step(rt, -1, "touch_menu");
    } else {
        trigger_sound_step(rt, -1, "touch_mini");
    }
}

static void on_sound_next_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    if (rt->input.menu_visible) {
        set_focus_obj(&rt->ui, rt->ui.btn_sound_next);
        trigger_sound_step(rt, 1, "touch_menu");
    } else {
        trigger_sound_step(rt, 1, "touch_mini");
    }
}

static void on_sound_test_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    if (rt->input.menu_visible) {
        set_focus_obj(&rt->ui, rt->ui.btn_sound_test);
        trigger_sound_play(rt, "touch_menu");
    } else {
        trigger_sound_play(rt, "touch_mini");
    }
}

static void on_nav_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    zvp_logf("click nav -> page=%u (tp=(%d,%d))",
             (unsigned)idx, (int)rt->diag_last_tp_x, (int)rt->diag_last_tp_y);
    zvp_ui_switch_page(&rt->ui, (zvp_ui_page_t)idx);
    set_focus_obj(&rt->ui, lv_event_get_target_obj(e));
    if (rt->ui.scr) {
        lv_obj_invalidate(rt->ui.scr);
    }
}

static void on_wifi_card_focus_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, lv_event_get_target_obj(e));
    zvp_logf("focus card -> wifi5g (tp=(%d,%d))",
             (int)rt->diag_last_tp_x, (int)rt->diag_last_tp_y);
}

static void on_wifi_badge_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, rt->ui.btn_wifi_card);
    trigger_wifi5g_toggle(rt, "touch");
}

static void on_channel_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, lv_event_get_target_obj(e));
    uintptr_t ch = (uintptr_t)lv_event_get_user_data(e);
    trigger_channel_select(rt, (uint8_t)ch, "touch");
}

static void on_c1_mode_focus_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, lv_event_get_target_obj(e));
    zvp_logf("focus card -> c1_mode (tp=(%d,%d))",
             (int)rt->diag_last_tp_x, (int)rt->diag_last_tp_y);
}

static void on_c1_badge_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, rt->ui.btn_c1_mode);
    trigger_c1_mode_toggle(rt, "touch");
}

static void on_flip_card_focus_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, lv_event_get_target_obj(e));
    zvp_logf("focus card -> auto_flip (tp=(%d,%d))",
             (int)rt->diag_last_tp_x, (int)rt->diag_last_tp_y);
}

static void on_flip_badge_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, rt->ui.btn_flip_card);
    trigger_auto_flip_toggle(rt, "touch");
}

static void on_native_touch_card_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, lv_event_get_target_obj(e));
    trigger_native_touch_toggle(rt, "touch_card");
}

static void on_native_touch_badge_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, rt->ui.btn_native_touch);
    trigger_native_touch_toggle(rt, "touch_badge");
}

static void trigger_craw_cycle_mode(zvp_runtime_t *rt, const char *src)
{
    if (!rt) return;
    zvp_logf("ui craw cycle begin: mode=%u src=%s",
             (unsigned)rt->craw.mode, src ? src : "?");
    int rc = zvp_craw_cycle_mode(&rt->craw);
    zvp_logf("ui craw cycle -> mode=%u rc=%d msg=%s src=%s",
             (unsigned)rt->craw.mode, rc, rt->craw.status_msg, src ? src : "?");
    zvp_ui_refresh(&rt->ui);
}

static void trigger_craw_pool_toggle(zvp_runtime_t *rt, const char *src)
{
    if (!rt) return;
    int rc = 0;
    if (rt->craw.pool_allocated) {
        if (rt->craw.mode == ZVP_CRAW_LIRO_ARM_2K || rt->craw.mode == ZVP_CRAW_LIRO_RAW_W0) {
            (void)zvp_craw_set_mode(&rt->craw, ZVP_CRAW_LIRO_SPY_ONLY);
        }
        zvp_craw_free_pool(&rt->craw);
    } else {
        rc = zvp_craw_alloc_pool(&rt->craw);
    }
    zvp_logf("ui craw pool -> alloc=%d rc=%d msg=%s src=%s",
             rt->craw.pool_allocated ? 1 : 0, rc, rt->craw.status_msg, src ? src : "?");
    zvp_ui_refresh(&rt->ui);
}

static void trigger_craw_snap(zvp_runtime_t *rt, const char *src)
{
    if (!rt) return;
    int rc = zvp_craw_dump_test_snapshot(&rt->craw);
    zvp_logf("ui craw snap -> rc=%d msg=%s src=%s",
             rc, rt->craw.status_msg, src ? src : "?");
    zvp_ui_refresh(&rt->ui);
}

static void trigger_craw_uninstall(zvp_runtime_t *rt, const char *src)
{
    if (!rt) return;
    zvp_craw_shutdown(&rt->craw);
    zvp_logf("ui craw shutdown -> msg=%s src=%s", rt->craw.status_msg, src ? src : "?");
    zvp_ui_refresh(&rt->ui);
}

static void on_craw_mode_focus_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, lv_event_get_target_obj(e));
}

static void on_craw_mode_badge_clicked(lv_event_t *e)
{
    (void)e;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, rt->ui.btn_craw_mode);
    trigger_craw_cycle_mode(rt, "touch_badge");
}

static void on_craw_pool_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, lv_event_get_target_obj(e));
    trigger_craw_pool_toggle(rt, "touch");
}

static void on_craw_snap_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, lv_event_get_target_obj(e));
    trigger_craw_snap(rt, "touch");
}

static void on_craw_uninstall_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, lv_event_get_target_obj(e));
    trigger_craw_uninstall(rt, "touch");
}

static void on_recovery_clicked(lv_event_t *e)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) return;
    set_focus_obj(&rt->ui, lv_event_get_target_obj(e));
    trigger_recovery(rt, "touch");
}

/* ============================================================================
 * 界面构建：全透明底 + 左侧 164px 一级菜单 + 右侧 452px 大字大热区二级区
 * ========================================================================== */

static void build_wifi5g_page(zvp_ui_t *ui, lv_obj_t *page)
{
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 10, 0);

    /* 1. 5GHz 总开关整行大卡片（430x94，点文字选中、点右侧胶囊直接开关） */
    ui->btn_wifi_card = create_click_card(page, 430, 94);
    lv_obj_set_flex_flow(ui->btn_wifi_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->btn_wifi_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(ui->btn_wifi_card, on_wifi_card_focus_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *sw_txt_box = lv_obj_create(ui->btn_wifi_card);
    lv_obj_remove_style_all(sw_txt_box);
    lv_obj_set_size(sw_txt_box, 274, 70);
    lv_obj_set_flex_flow(sw_txt_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sw_txt_box, 4, 0);
    lv_obj_clear_flag(sw_txt_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(sw_txt_box, LV_OBJ_FLAG_SCROLLABLE);
    make_label(sw_txt_box, "解锁 5GHz 频段", &zvp_font_zh24, ZVP_COLOR_WHITE);
    ui->lbl_wifi_status = make_label(sw_txt_box, "待机 (原厂 2.4GHz)", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);

    ui->badge_wifi5g = make_state_badge(ui->btn_wifi_card, 114, 54, "已关闭", &ui->lbl_wifi5g_badge);
    lv_obj_add_flag(ui->badge_wifi5g, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui->badge_wifi5g, on_wifi_badge_clicked, LV_EVENT_CLICKED, NULL);

    /* 2. 频段与全信道大卡片（430x338，5x2 超大触控按钮） */
    lv_obj_t *ch_card = create_card(page, 430, 338);
    lv_obj_set_flex_flow(ch_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ch_card, 12, 0);

    make_label(ch_card, "频段与信道", &zvp_font_zh24, ZVP_COLOR_WHITE);

    ui->ch_grid = lv_obj_create(ch_card);
    lv_obj_remove_style_all(ui->ch_grid);
    lv_obj_set_size(ui->ch_grid, 402, 266);
    lv_obj_set_flex_flow(ui->ch_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(ui->ch_grid, 14, 0);
    lv_obj_set_style_pad_column(ui->ch_grid, 8, 0);
    lv_obj_clear_flag(ui->ch_grid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui->ch_grid, LV_OBJ_FLAG_SCROLLABLE);

    size_t cnt = 0;
    const uint8_t *channels = zvp_wifi5g_get_channels(&cnt);
    for (size_t i = 0; i < cnt && i < ZVP_WIFI5G_CHANNEL_COUNT; ++i) {
        lv_obj_t *btn = lv_button_create(ui->ch_grid);
        lv_obj_set_size(btn, 74, 118);
        style_mono_btn(btn);
        lv_obj_add_event_cb(btn, on_channel_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)channels[i]);

        char buf[16];
        if (channels[i] == ZVP_WIFI_CH_AUTO) {
            snprintf(buf, sizeof(buf), "Auto");
        } else {
            snprintf(buf, sizeof(buf), "%u", (unsigned)channels[i]);
        }
        lv_obj_t *lbl = make_label(btn, buf, &zvp_font_zh24, ZVP_COLOR_WHITE);
        lv_obj_center(lbl);
        ui->ch_btns[i] = btn;
    }
}

static void build_settings_page(zvp_ui_t *ui, lv_obj_t *page)
{
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 10, 0);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    /* 1. C1 唤醒按键切换整行可点大卡片 */
    ui->btn_c1_mode = create_click_card(page, 430, 78);
    lv_obj_set_flex_flow(ui->btn_c1_mode, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->btn_c1_mode, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(ui->btn_c1_mode, on_c1_mode_focus_clicked, LV_EVENT_CLICKED, NULL);
    make_label(ui->btn_c1_mode, "C1 按键唤醒", &zvp_font_zh24, ZVP_COLOR_WHITE);

    ui->badge_c1_mode = make_state_badge(ui->btn_c1_mode, 162, 48, "短按独占", &ui->lbl_c1_mode);
    lv_obj_add_flag(ui->badge_c1_mode, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui->badge_c1_mode, on_c1_badge_clicked, LV_EVENT_CLICKED, NULL);

    /* 2. 屏幕翻转自适应整行可点大卡片 */
    ui->btn_flip_card = create_click_card(page, 430, 82);
    lv_obj_set_flex_flow(ui->btn_flip_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->btn_flip_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(ui->btn_flip_card, on_flip_card_focus_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *flip_txt = lv_obj_create(ui->btn_flip_card);
    lv_obj_remove_style_all(flip_txt);
    lv_obj_set_size(flip_txt, 274, 60);
    lv_obj_set_flex_flow(flip_txt, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(flip_txt, 2, 0);
    lv_obj_clear_flag(flip_txt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(flip_txt, LV_OBJ_FLAG_SCROLLABLE);
    make_label(flip_txt, "屏幕翻转自适应", &zvp_font_zh24, ZVP_COLOR_WHITE);
    ui->lbl_flip_state = make_label(flip_txt, "自动跟随 · 正向", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);

    ui->badge_auto_flip = make_state_badge(ui->btn_flip_card, 114, 48, "已开启", &ui->lbl_auto_flip_badge);
    lv_obj_add_flag(ui->badge_auto_flip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui->badge_auto_flip, on_flip_badge_clicked, LV_EVENT_CLICKED, NULL);

    /* 3. 原厂 MENU 主菜单与 Fn 菜单全界面触控 (MTF2) */
    ui->btn_native_touch = create_click_card(page, 430, 82);
    lv_obj_set_flex_flow(ui->btn_native_touch, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->btn_native_touch, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(ui->btn_native_touch, on_native_touch_card_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *mtf_txt = lv_obj_create(ui->btn_native_touch);
    lv_obj_remove_style_all(mtf_txt);
    lv_obj_set_size(mtf_txt, 274, 60);
    lv_obj_set_flex_flow(mtf_txt, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(mtf_txt, 2, 0);
    lv_obj_clear_flag(mtf_txt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(mtf_txt, LV_OBJ_FLAG_SCROLLABLE);
    make_label(mtf_txt, "原厂菜单/Fn触控", &zvp_font_zh24, ZVP_COLOR_WHITE);
    ui->lbl_native_touch_state = make_label(mtf_txt, "主菜单·弹窗·Fn 全触控", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);

    ui->badge_native_touch = make_state_badge(ui->btn_native_touch, 114, 48, "已开启", &ui->lbl_native_touch_badge);
    lv_obj_add_flag(ui->badge_native_touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui->badge_native_touch, on_native_touch_badge_clicked, LV_EVENT_CLICKED, NULL);

    /* 4. 安全复位大卡片 */
    lv_obj_t *rec_card = create_card(page, 430, 128);
    lv_obj_set_flex_flow(rec_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(rec_card, 8, 0);

    ui->btn_recovery = lv_button_create(rec_card);
    lv_obj_set_size(ui->btn_recovery, 400, 62);
    style_mono_btn(ui->btn_recovery);
    lv_obj_add_event_cb(ui->btn_recovery, on_recovery_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rec_btn_lbl = make_label(ui->btn_recovery, "安全复位 (全量恢复默认)", &zvp_font_zh24, ZVP_COLOR_WHITE);
    lv_obj_center(rec_btn_lbl);

    ui->lbl_recovery = make_label(rec_card, "就绪", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);

    /* 5. 卡控熔断门禁简况 */
    ui->lbl_gate_info = make_label(page, "卡控: 已插卡激活 · B:/ZVP_OFF 可禁用", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);
}

static void build_custom_shutter_page(zvp_ui_t *ui, lv_obj_t *page)
{
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 10, 0);

    /* 1. 自定义快门音总开关 (430x88) */
    ui->btn_shutter_enable = create_click_card(page, 430, 88);
    lv_obj_set_flex_flow(ui->btn_shutter_enable, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->btn_shutter_enable, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(ui->btn_shutter_enable, on_shutter_focus_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *sh_txt = lv_obj_create(ui->btn_shutter_enable);
    lv_obj_remove_style_all(sh_txt);
    lv_obj_set_size(sh_txt, 274, 64);
    lv_obj_set_flex_flow(sh_txt, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sh_txt, 4, 0);
    lv_obj_clear_flag(sh_txt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(sh_txt, LV_OBJ_FLAG_SCROLLABLE);
    make_label(sh_txt, "自定义快门音", &zvp_font_zh24, ZVP_COLOR_WHITE);
    ui->lbl_shutter_status = make_label(sh_txt, "已关闭", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);

    ui->badge_shutter_enable = make_state_badge(ui->btn_shutter_enable, 114, 52, "已关闭", &ui->lbl_shutter_badge);
    lv_obj_add_flag(ui->badge_shutter_enable, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui->badge_shutter_enable, on_shutter_badge_clicked, LV_EVENT_CLICKED, NULL);

    /* 2. 左上角小悬浮窗开关 (430x80) */
    ui->btn_mini_enable = create_click_card(page, 430, 80);
    lv_obj_set_flex_flow(ui->btn_mini_enable, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->btn_mini_enable, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(ui->btn_mini_enable, on_mini_focus_clicked, LV_EVENT_CLICKED, NULL);
    make_label(ui->btn_mini_enable, "拍摄界面小悬浮窗", &zvp_font_zh24, ZVP_COLOR_WHITE);

    ui->badge_mini_enable = make_state_badge(ui->btn_mini_enable, 114, 50, "已关闭", &ui->lbl_mini_badge);
    lv_obj_add_flag(ui->badge_mini_enable, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui->badge_mini_enable, on_mini_badge_clicked, LV_EVENT_CLICKED, NULL);

    /* 3. 快门音频选择卡片 (430x154) */
    ui->btn_sound_card = create_card(page, 430, 154);
    lv_obj_set_flex_flow(ui->btn_sound_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui->btn_sound_card, 8, 0);

    make_label(ui->btn_sound_card, "快门音频选择", &zvp_font_zh24, ZVP_COLOR_WHITE);
    ui->lbl_sound_src = make_label(ui->btn_sound_card, "内置预设 · SD卡目录: PRIVATE/M4ROOT/GENERAL/SOUNDS", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);

    lv_obj_t *sel_row = lv_obj_create(ui->btn_sound_card);
    lv_obj_remove_style_all(sel_row);
    lv_obj_set_size(sel_row, 400, 60);
    lv_obj_set_flex_flow(sel_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sel_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(sel_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(sel_row, LV_OBJ_FLAG_SCROLLABLE);

    ui->btn_sound_prev = lv_button_create(sel_row);
    lv_obj_set_size(ui->btn_sound_prev, 62, 54);
    style_mono_btn(ui->btn_sound_prev);
    lv_obj_add_event_cb(ui->btn_sound_prev, on_sound_prev_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_prev = make_label(ui->btn_sound_prev, "<", &zvp_font_zh24, ZVP_COLOR_WHITE);
    lv_obj_center(lbl_prev);

    ui->lbl_sound_name = make_label(sel_row, "内置: 清脆快门", &zvp_font_zh24, ZVP_COLOR_WHITE);
    lv_obj_set_width(ui->lbl_sound_name, 260);
    lv_label_set_long_mode(ui->lbl_sound_name, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(ui->lbl_sound_name, LV_TEXT_ALIGN_CENTER, 0);

    ui->btn_sound_next = lv_button_create(sel_row);
    lv_obj_set_size(ui->btn_sound_next, 62, 54);
    style_mono_btn(ui->btn_sound_next);
    lv_obj_add_event_cb(ui->btn_sound_next, on_sound_next_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_next = make_label(ui->btn_sound_next, ">", &zvp_font_zh24, ZVP_COLOR_WHITE);
    lv_obj_center(lbl_next);

    /* 4. 试听播放大按钮 (430x88) */
    ui->btn_sound_test = create_click_card(page, 430, 88);
    lv_obj_set_flex_flow(ui->btn_sound_test, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui->btn_sound_test, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ui->btn_sound_test, 4, 0);
    lv_obj_add_event_cb(ui->btn_sound_test, on_sound_test_clicked, LV_EVENT_CLICKED, NULL);
    make_label(ui->btn_sound_test, "试听当前快门音", &zvp_font_zh24, ZVP_COLOR_WHITE);
    ui->lbl_sound_test = make_label(ui->btn_sound_test, "点击或按 OK 立即播放", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);
}

static void build_mini_widget(zvp_ui_t *ui, lv_obj_t *parent)
{
    ui->mini_win = lv_obj_create(parent);
    lv_obj_set_pos(ui->mini_win, ZVP_MINI_WIDGET_X, ZVP_MINI_WIDGET_Y);
    lv_obj_set_size(ui->mini_win, ZVP_MINI_WIDGET_W, ZVP_MINI_WIDGET_H);
    lv_obj_set_style_bg_color(ui->mini_win, ZVP_COLOR_BG_BLACK, 0);
    lv_obj_set_style_bg_opa(ui->mini_win, 165, 0);
    lv_obj_set_style_border_color(ui->mini_win, ZVP_COLOR_WHITE, 0);
    lv_obj_set_style_border_width(ui->mini_win, 1, 0);
    lv_obj_set_style_border_opa(ui->mini_win, 180, 0);
    lv_obj_set_style_radius(ui->mini_win, 10, 0);
    lv_obj_set_style_shadow_width(ui->mini_win, 0, 0);
    lv_obj_set_style_pad_all(ui->mini_win, 4, 0);
    lv_obj_set_style_pad_column(ui->mini_win, 4, 0);
    lv_obj_set_flex_flow(ui->mini_win, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->mini_win, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(ui->mini_win, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui->mini_win, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui->mini_win, LV_OBJ_FLAG_CLICKABLE);

    ui->btn_mini_prev = lv_button_create(ui->mini_win);
    lv_obj_set_size(ui->btn_mini_prev, 38, 36);
    style_mono_btn(ui->btn_mini_prev);
    lv_obj_add_event_cb(ui->btn_mini_prev, on_sound_prev_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *mp_lbl = make_label(ui->btn_mini_prev, "<", &zvp_font_zh18, ZVP_COLOR_WHITE);
    lv_obj_center(mp_lbl);

    ui->btn_mini_play = lv_button_create(ui->mini_win);
    lv_obj_set_size(ui->btn_mini_play, 154, 36);
    style_mono_btn(ui->btn_mini_play);
    lv_obj_set_style_pad_hor(ui->btn_mini_play, 4, 0);
    lv_obj_add_event_cb(ui->btn_mini_play, on_sound_test_clicked, LV_EVENT_CLICKED, NULL);
    ui->lbl_mini_play = make_label(ui->btn_mini_play, "播放", &zvp_font_zh18, ZVP_COLOR_WHITE);
    lv_obj_set_width(ui->lbl_mini_play, 144);
    lv_label_set_long_mode(ui->lbl_mini_play, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(ui->lbl_mini_play, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(ui->lbl_mini_play);

    ui->btn_mini_next = lv_button_create(ui->mini_win);
    lv_obj_set_size(ui->btn_mini_next, 38, 36);
    style_mono_btn(ui->btn_mini_next);
    lv_obj_add_event_cb(ui->btn_mini_next, on_sound_next_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *mn_lbl = make_label(ui->btn_mini_next, ">", &zvp_font_zh18, ZVP_COLOR_WHITE);
    lv_obj_center(mn_lbl);

    lv_obj_add_flag(ui->mini_win, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *make_aspect_overlay_rect(lv_obj_t *parent, lv_color_t color, uint8_t bg_opa)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, bg_opa, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    return obj;
}

static void build_aspect_overlay(zvp_ui_t *ui, lv_obj_t *parent)
{
    ui->aspect_layer = lv_obj_create(parent);
    lv_obj_remove_style_all(ui->aspect_layer);
    lv_obj_set_pos(ui->aspect_layer, 0, 0);
    lv_obj_set_size(ui->aspect_layer, ZVP_SCREEN_WIDTH, ZVP_SCREEN_HEIGHT);
    lv_obj_set_style_bg_opa(ui->aspect_layer, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(ui->aspect_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui->aspect_layer, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 4; ++i) {
        ui->aspect_bars[i] = make_aspect_overlay_rect(ui->aspect_layer, ZVP_COLOR_BG_BLACK, 115);
    }

    ui->aspect_frame_box = lv_obj_create(ui->aspect_layer);
    lv_obj_remove_style_all(ui->aspect_frame_box);
    lv_obj_set_style_bg_opa(ui->aspect_frame_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ui->aspect_frame_box, ZVP_COLOR_WHITE, 0);
    lv_obj_set_style_border_width(ui->aspect_frame_box, 1, 0);
    lv_obj_set_style_border_opa(ui->aspect_frame_box, 180, 0);
    lv_obj_set_style_radius(ui->aspect_frame_box, 0, 0);
    lv_obj_clear_flag(ui->aspect_frame_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui->aspect_frame_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui->aspect_frame_box, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 4; ++i) {
        ui->aspect_thirds[i] = make_aspect_overlay_rect(ui->aspect_layer, ZVP_COLOR_WHITE, 120);
    }

    lv_obj_add_flag(ui->aspect_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(ui->aspect_layer);
}

static lv_obj_t *make_aspect_opt_btn(lv_obj_t *parent, const char *title, const char *init_val, lv_obj_t **out_lbl)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 128, 98);
    style_mono_btn(btn);
    lv_obj_set_style_pad_hor(btn, 6, 0);
    lv_obj_set_style_pad_ver(btn, 10, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btn, 6, 0);

    make_label(btn, title, &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);
    lv_obj_t *val = make_label(btn, init_val, &zvp_font_zh24, ZVP_COLOR_WHITE);
    if (out_lbl) {
        *out_lbl = val;
    }
    return btn;
}

static void build_aspect_page(zvp_ui_t *ui, lv_obj_t *page)
{
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 10, 0);

    /* 1. 纵横比参考遮罩总开关 (430x84) */
    ui->btn_aspect_enable = create_click_card(page, 430, 84);
    lv_obj_set_flex_flow(ui->btn_aspect_enable, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->btn_aspect_enable, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(ui->btn_aspect_enable, on_aspect_focus_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *txt_box = lv_obj_create(ui->btn_aspect_enable);
    lv_obj_remove_style_all(txt_box);
    lv_obj_set_size(txt_box, 274, 62);
    lv_obj_set_flex_flow(txt_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(txt_box, 3, 0);
    lv_obj_clear_flag(txt_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(txt_box, LV_OBJ_FLAG_SCROLLABLE);
    make_label(txt_box, "纵横比取景遮罩", &zvp_font_zh24, ZVP_COLOR_WHITE);
    ui->lbl_aspect_status = make_label(txt_box, "已关闭 · 仅实时取景参考", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);

    ui->badge_aspect_enable = make_state_badge(ui->btn_aspect_enable, 114, 52, "已关闭", &ui->lbl_aspect_badge);
    lv_obj_add_flag(ui->badge_aspect_enable, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui->badge_aspect_enable, on_aspect_badge_clicked, LV_EVENT_CLICKED, NULL);

    /* 2. 12 档构图纵横比 4x3 触控网格 (430x210) */
    lv_obj_t *grid_card = create_card(page, 430, 210);
    lv_obj_set_style_pad_ver(grid_card, 11, 0);
    ui->aspect_grid = lv_obj_create(grid_card);
    lv_obj_remove_style_all(ui->aspect_grid);
    lv_obj_set_size(ui->aspect_grid, 402, 188);
    lv_obj_set_flex_flow(ui->aspect_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(ui->aspect_grid, 8, 0);
    lv_obj_set_style_pad_column(ui->aspect_grid, 8, 0);
    lv_obj_clear_flag(ui->aspect_grid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui->aspect_grid, LV_OBJ_FLAG_SCROLLABLE);

    size_t cnt = 0;
    const zvp_aspect_preset_t *presets = zvp_aspect_get_presets(&cnt);
    for (size_t i = 0; i < cnt && i < ZVP_ASPECT_PRESET_COUNT; ++i) {
        lv_obj_t *btn = lv_button_create(ui->aspect_grid);
        lv_obj_set_size(btn, 94, 56);
        style_mono_btn(btn);
        lv_obj_add_event_cb(btn, on_aspect_preset_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *lbl = make_label(btn, presets[i].label, &zvp_font_zh24, ZVP_COLOR_WHITE);
        lv_obj_center(lbl);
        ui->aspect_btns[i] = btn;
    }

    /* 3. 遮罩外观 / 辅助线 / 屏幕基准三项调节卡 (430x126) */
    lv_obj_t *opt_card = create_card(page, 430, 126);
    lv_obj_set_style_pad_ver(opt_card, 13, 0);
    lv_obj_t *opt_row = lv_obj_create(opt_card);
    lv_obj_remove_style_all(opt_row);
    lv_obj_set_size(opt_row, 402, 100);
    lv_obj_set_flex_flow(opt_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(opt_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(opt_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(opt_row, LV_OBJ_FLAG_SCROLLABLE);

    ui->btn_aspect_style = make_aspect_opt_btn(opt_row, "遮罩浓度", "45%压暗", &ui->lbl_aspect_style);
    lv_obj_add_event_cb(ui->btn_aspect_style, on_aspect_style_clicked, LV_EVENT_CLICKED, NULL);

    ui->btn_aspect_guide = make_aspect_opt_btn(opt_row, "参考辅助线", "构图框线", &ui->lbl_aspect_guide);
    lv_obj_add_event_cb(ui->btn_aspect_guide, on_aspect_guide_clicked, LV_EVENT_CLICKED, NULL);

    ui->btn_aspect_par = make_aspect_opt_btn(opt_row, "取景基准", "自动跟画面", &ui->lbl_aspect_par);
    lv_obj_add_event_cb(ui->btn_aspect_par, on_aspect_par_clicked, LV_EVENT_CLICKED, NULL);
}

static lv_obj_t *make_craw_action_btn(lv_obj_t *parent, const char *title,
                                      const char *init_sub, lv_obj_t **out_sub)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 128, 88);
    style_mono_btn(btn);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btn, 4, 0);

    make_label(btn, title, &zvp_font_zh18, ZVP_COLOR_WHITE);
    lv_obj_t *sub = make_label(btn, init_sub, &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);
    if (out_sub) {
        *out_sub = sub;
    }
    return btn;
}

static void build_craw_page(zvp_ui_t *ui, lv_obj_t *page)
{
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 10, 0);

    /* 1. 2K cRAW 模式总开关卡片 (430x92) */
    ui->btn_craw_mode = create_click_card(page, 430, 92);
    lv_obj_set_flex_flow(ui->btn_craw_mode, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->btn_craw_mode, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(ui->btn_craw_mode, on_craw_mode_focus_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *txt_box = lv_obj_create(ui->btn_craw_mode);
    lv_obj_remove_style_all(txt_box);
    lv_obj_set_size(txt_box, 274, 66);
    lv_obj_set_flex_flow(txt_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(txt_box, 4, 0);
    lv_obj_clear_flag(txt_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(txt_box, LV_OBJ_FLAG_SCROLLABLE);
    make_label(txt_box, "2K cRAW 纯内存", &zvp_font_zh24, ZVP_COLOR_WHITE);
    ui->lbl_craw_mode_desc = make_label(txt_box, "未挂载 · 原厂流程", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);

    ui->badge_craw_mode = make_state_badge(ui->btn_craw_mode, 114, 52, "旁路", &ui->lbl_craw_mode_badge);
    lv_obj_add_flag(ui->badge_craw_mode, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui->badge_craw_mode, on_craw_mode_badge_clicked, LV_EVENT_CLICKED, NULL);

    /* 2. 三联操作按钮卡片 (430x114) */
    lv_obj_t *act_card = create_card(page, 430, 114);
    lv_obj_set_style_pad_ver(act_card, 13, 0);
    lv_obj_t *act_row = lv_obj_create(act_card);
    lv_obj_remove_style_all(act_row);
    lv_obj_set_size(act_row, 402, 88);
    lv_obj_set_flex_flow(act_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(act_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(act_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(act_row, LV_OBJ_FLAG_SCROLLABLE);

    ui->btn_craw_pool = make_craw_action_btn(act_row, "DMM 缓冲", "未分配", &ui->lbl_craw_pool);
    lv_obj_add_event_cb(ui->btn_craw_pool, on_craw_pool_clicked, LV_EVENT_CLICKED, NULL);

    ui->btn_craw_snap = make_craw_action_btn(act_row, "写卡快照", "8MiB直写", &ui->lbl_craw_snap);
    lv_obj_add_event_cb(ui->btn_craw_snap, on_craw_snap_clicked, LV_EVENT_CLICKED, NULL);

    ui->btn_craw_uninstall = make_craw_action_btn(act_row, "安全卸载", "恢复原厂", &ui->lbl_craw_uninstall);
    lv_obj_add_event_cb(ui->btn_craw_uninstall, on_craw_uninstall_clicked, LV_EVENT_CLICKED, NULL);

    /* 3. 实时遥测与物理地址监测卡片 (430x216) */
    lv_obj_t *tel_card = create_card(page, 430, 216);
    lv_obj_set_flex_flow(tel_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tel_card, 10, 0);

    make_label(tel_card, "LIRO 遥测与帧槽", &zvp_font_zh24, ZVP_COLOR_WHITE);
    ui->lbl_craw_telem_1 = make_label(tel_card, "Rdc:0 Seq:0 Aif:0 ISR:0", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);
    lv_label_set_long_mode(ui->lbl_craw_telem_1, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui->lbl_craw_telem_1, 400);

    ui->lbl_craw_telem_2 = make_label(tel_card, "1920x1088 enc=2 | SD:-- MB", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);
    lv_label_set_long_mode(ui->lbl_craw_telem_2, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui->lbl_craw_telem_2, 400);
}

static void build_lut_page(zvp_ui_t *ui, lv_obj_t *page)
{
    /* 二级菜单整体保持极窄纯列表 (98x408)，顶部大图标横栏位于其正上方 */
    ui->lut_list = lv_obj_create(page);
    lv_obj_remove_style_all(ui->lut_list);
    lv_obj_set_size(ui->lut_list, 98, 408);
    lv_obj_set_flex_flow(ui->lut_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui->lut_list, 5, 0);
    lv_obj_set_style_bg_opa(ui->lut_list, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(ui->lut_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui->lut_list, LV_DIR_VER);
    lv_obj_clear_flag(ui->lut_list, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scrollbar_mode(ui->lut_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(ui->lut_list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(ui->lut_list, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(ui->lut_list, ZVP_COLOR_WHITE, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(ui->lut_list, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(ui->lut_list, 2, LV_PART_SCROLLBAR);

    for (uint32_t i = 0u; i < ZVP_LUT_NUM_SLOTS; i++) {
        lv_obj_t *row = lv_button_create(ui->lut_list);
        lv_obj_set_size(row, 94, 46);
        style_mono_btn(row);
        lv_obj_set_style_pad_hor(row, 3, 0);
        lv_obj_set_style_pad_ver(row, 4, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(row, on_lut_row_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        lv_obj_t *lbl_name = make_label(row, "LUT", &zvp_font_zh18, ZVP_COLOR_WHITE);
        lv_obj_set_width(lbl_name, 74);
        lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_anim_duration(lbl_name, 3200, 0);

        lv_obj_t *lbl_badge = make_label(row, "", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);
        lv_obj_set_style_text_align(lbl_badge, LV_TEXT_ALIGN_RIGHT, 0);

        ui->lut_row_btns[i] = row;
        ui->lut_row_badges[i] = lbl_badge;
    }
}

static void build_lut_top_layer(zvp_ui_t *ui, lv_obj_t *parent)
{
    /* 置于滤镜二级菜单正上方的大号横栏：[⊘关闭] [索尼] [富士] [理光] [SD导入] (110, 8, 266x42) */
    ui->lut_top_layer = lv_obj_create(parent);
    lv_obj_set_pos(ui->lut_top_layer, 110, 8);
    lv_obj_set_size(ui->lut_top_layer, 266, 42);
    style_glass_panel(ui->lut_top_layer, 8);
    lv_obj_set_style_pad_all(ui->lut_top_layer, 3, 0);
    lv_obj_set_flex_flow(ui->lut_top_layer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->lut_top_layer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ui->lut_top_layer, 3, 0);
    lv_obj_set_scrollbar_mode(ui->lut_top_layer, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui->lut_top_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui->lut_top_layer, LV_OBJ_FLAG_SCROLLABLE);

    ui->btn_lut_off = lv_button_create(ui->lut_top_layer);
    lv_obj_set_size(ui->btn_lut_off, 42, 34);
    style_mono_btn(ui->btn_lut_off);
    lv_obj_set_style_radius(ui->btn_lut_off, 6, 0);
    lv_obj_set_style_pad_hor(ui->btn_lut_off, 1, 0);
    lv_obj_set_style_pad_ver(ui->btn_lut_off, 1, 0);
    lv_obj_add_event_cb(ui->btn_lut_off, on_lut_off_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_off = make_label(ui->btn_lut_off, "\xee\x80\x88", &zvp_font_lut_top_icons, ZVP_COLOR_WHITE);
    lv_obj_center(lbl_off);

    static const char *const k_cat_names[ZVP_LUT_CAT_COUNT] = { "索尼", "富士", "理光" };
    for (uint32_t c = 0u; c < ZVP_LUT_CAT_COUNT; c++) {
        lv_obj_t *cbtn = lv_button_create(ui->lut_top_layer);
        lv_obj_set_size(cbtn, 54, 34);
        style_mono_btn(cbtn);
        lv_obj_set_style_radius(cbtn, 6, 0);
        lv_obj_set_style_pad_hor(cbtn, 1, 0);
        lv_obj_set_style_pad_ver(cbtn, 1, 0);
        lv_obj_add_event_cb(cbtn, on_lut_cat_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)c);
        lv_obj_t *clbl = make_label(cbtn, k_cat_names[c], &zvp_font_zh20, ZVP_COLOR_WHITE);
        lv_obj_center(clbl);
        ui->lut_cat_btns[c] = cbtn;
    }

    ui->btn_lut_sd = lv_button_create(ui->lut_top_layer);
    lv_obj_set_size(ui->btn_lut_sd, 42, 34);
    style_mono_btn(ui->btn_lut_sd);
    lv_obj_set_style_radius(ui->btn_lut_sd, 6, 0);
    lv_obj_set_style_pad_hor(ui->btn_lut_sd, 1, 0);
    lv_obj_set_style_pad_ver(ui->btn_lut_sd, 1, 0);
    lv_obj_add_event_cb(ui->btn_lut_sd, on_lut_sd_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_sd = make_label(ui->btn_lut_sd, "\xee\x80\x89", &zvp_font_lut_top_icons, ZVP_COLOR_WHITE);
    lv_obj_center(lbl_sd);

    lv_obj_add_flag(ui->lut_top_layer, LV_OBJ_FLAG_HIDDEN);
}

static void build_lut_fine_layer(zvp_ui_t *ui, lv_obj_t *parent)
{
    /* 右侧预览区水平居中、悬浮于原厂 52px 底部黑条上方 8px (261, 378, 334x42)，容器全透无大外框 */
    ui->lut_fine_layer = lv_obj_create(parent);
    lv_obj_remove_style_all(ui->lut_fine_layer);
    lv_obj_set_pos(ui->lut_fine_layer, 261, 378);
    lv_obj_set_size(ui->lut_fine_layer, 334, 42);
    lv_obj_set_style_bg_opa(ui->lut_fine_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(ui->lut_fine_layer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->lut_fine_layer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ui->lut_fine_layer, 2, 0);
    lv_obj_clear_flag(ui->lut_fine_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui->lut_fine_layer, LV_OBJ_FLAG_SCROLLABLE);

    /* 对应 tools/gen_zh_font.py 生成的 U+E000..U+E006 16x16 摄影图标 */
    static const char *const k_param_icons[ZVP_LUT_PARAM_COUNT] = {
        "\xee\x80\x80", /* 强度 */
        "\xee\x80\x81", /* 对比 */
        "\xee\x80\x82", /* 高光 */
        "\xee\x80\x83", /* 阴影 */
        "\xee\x80\x84", /* 褪色 */
        "\xee\x80\x85", /* 饱和 */
        "\xee\x80\x86", /* 锐度 */
    };
    for (uint32_t i = 0u; i < ZVP_LUT_PARAM_COUNT; i++) {
        lv_obj_t *tile = lv_button_create(ui->lut_fine_layer);
        lv_obj_set_size(tile, 40, 42);
        style_fine_tile(tile);
        lv_obj_set_style_pad_hor(tile, 0, 0);
        lv_obj_set_style_pad_ver(tile, 2, 0);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(tile, -3, 0);
        lv_obj_add_event_cb(tile, on_lut_param_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        make_label(tile, k_param_icons[i], &zvp_font_lut_icons, ZVP_COLOR_WHITE);
        ui->lut_param_vals[i] = make_label(tile, "0", &zvp_font_zh18, ZVP_COLOR_WHITE);
        lv_label_set_long_mode(ui->lut_param_vals[i], LV_LABEL_LONG_CLIP);
        ui->lut_param_btns[i] = tile;
    }

    ui->btn_lut_fine_reset = lv_button_create(ui->lut_fine_layer);
    lv_obj_set_size(ui->btn_lut_fine_reset, 40, 42);
    style_fine_tile(ui->btn_lut_fine_reset);
    lv_obj_set_style_pad_hor(ui->btn_lut_fine_reset, 0, 0);
    lv_obj_set_style_pad_ver(ui->btn_lut_fine_reset, 2, 0);
    lv_obj_set_flex_flow(ui->btn_lut_fine_reset, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui->btn_lut_fine_reset, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ui->btn_lut_fine_reset, -3, 0);
    lv_obj_add_event_cb(ui->btn_lut_fine_reset, on_lut_fine_reset_clicked, LV_EVENT_CLICKED, NULL);
    make_label(ui->btn_lut_fine_reset, "\xee\x80\x87", &zvp_font_lut_icons, ZVP_COLOR_WHITE);
    lv_obj_t *lbl_rst = make_label(ui->btn_lut_fine_reset, "重置", &zvp_font_zh18, ZVP_COLOR_WHITE);
    lv_label_set_long_mode(lbl_rst, LV_LABEL_LONG_CLIP);

    lv_obj_add_flag(ui->lut_fine_layer, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *make_cal_side_btn(lv_obj_t *parent, int32_t h, const char *title,
                                   const char *init_sub, lv_obj_t **out_sub)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 122, h);
    style_mono_btn(btn);
    lv_obj_set_style_pad_hor(btn, 4, 0);
    lv_obj_set_style_pad_ver(btn, 4, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btn, 2, 0);

    make_label(btn, title, &zvp_font_zh20, ZVP_COLOR_WHITE);
    if (out_sub) {
        *out_sub = make_label(btn, init_sub, &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);
        lv_label_set_long_mode(*out_sub, LV_LABEL_LONG_CLIP);
    }
    return btn;
}

static void build_color_cal_page(zvp_ui_t *ui, lv_obj_t *page)
{
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 6, 0);

    /* 1. 总开关按钮卡 (122x74) */
    ui->btn_cal_enable = create_click_card(page, 122, 74);
    lv_obj_set_style_pad_hor(ui->btn_cal_enable, 6, 0);
    lv_obj_set_style_pad_ver(ui->btn_cal_enable, 5, 0);
    lv_obj_set_flex_flow(ui->btn_cal_enable, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui->btn_cal_enable, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ui->btn_cal_enable, 4, 0);
    lv_obj_add_event_cb(ui->btn_cal_enable, on_cal_enable_clicked, LV_EVENT_CLICKED, NULL);
    make_label(ui->btn_cal_enable, "屏幕校色", &zvp_font_zh20, ZVP_COLOR_WHITE);
    ui->badge_cal_enable = make_state_badge(ui->btn_cal_enable, 104, 30, "已关闭", &ui->lbl_cal_badge);
    lv_obj_set_style_text_font(ui->lbl_cal_badge, &zvp_font_zh18, 0);

    /* 2. 一键对比原色按钮 (122x66) */
    ui->btn_cal_compare = make_cal_side_btn(page, 66, "对比原色", "切换基准", &ui->lbl_cal_compare);
    lv_obj_add_event_cb(ui->btn_cal_compare, on_cal_compare_clicked, LV_EVENT_CLICKED, NULL);

    /* 3. 校色预设循环按钮 (122x66) */
    ui->btn_cal_preset = make_cal_side_btn(page, 66, "校色预设", "自定义", &ui->lbl_cal_preset);
    lv_obj_add_event_cb(ui->btn_cal_preset, on_cal_preset_clicked, LV_EVENT_CLICKED, NULL);

    /* 4. 全部参数归零 (122x52) */
    ui->btn_cal_reset = lv_button_create(page);
    lv_obj_set_size(ui->btn_cal_reset, 122, 52);
    style_mono_btn(ui->btn_cal_reset);
    lv_obj_add_event_cb(ui->btn_cal_reset, on_cal_reset_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_z = make_label(ui->btn_cal_reset, "全部归零", &zvp_font_zh20, ZVP_COLOR_WHITE);
    lv_obj_center(lbl_z);

    /* 5. 状态摘要卡 (122x170，只读透传运行信息，无遮挡中央取景区) */
    lv_obj_t *st_card = create_card(page, 122, 166);
    lv_obj_set_style_pad_hor(st_card, 6, 0);
    lv_obj_set_style_pad_ver(st_card, 6, 0);
    lv_obj_set_flex_flow(st_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(st_card, 4, 0);
    make_label(st_card, "11bit 仅屏显", &zvp_font_zh18, ZVP_COLOR_WHITE);
    ui->lbl_cal_status = make_label(st_card, "纯RAM无损", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);
    lv_obj_set_width(ui->lbl_cal_status, 108);
    lv_label_set_long_mode(ui->lbl_cal_status, LV_LABEL_LONG_WRAP);
}

static void build_cal_top_layer(zvp_ui_t *ui, lv_obj_t *parent)
{
    /* 右上角极窄步进胶囊 (252, 8, 380x40)，中间留出超大透明实景取景窗 */
    ui->cal_top_layer = lv_obj_create(parent);
    lv_obj_set_pos(ui->cal_top_layer, 252, 8);
    lv_obj_set_size(ui->cal_top_layer, 380, 40);
    style_glass_panel(ui->cal_top_layer, 8);
    lv_obj_set_style_pad_hor(ui->cal_top_layer, 8, 0);
    lv_obj_set_style_pad_ver(ui->cal_top_layer, 4, 0);
    lv_obj_set_flex_flow(ui->cal_top_layer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->cal_top_layer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ui->cal_top_layer, 6, 0);
    lv_obj_clear_flag(ui->cal_top_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui->cal_top_layer, LV_OBJ_FLAG_SCROLLABLE);

    ui->lbl_cal_top_param = make_label(ui->cal_top_layer, "色温: 0", &zvp_font_zh18, ZVP_COLOR_WHITE);
    lv_obj_set_width(ui->lbl_cal_top_param, 254);
    lv_label_set_long_mode(ui->lbl_cal_top_param, LV_LABEL_LONG_CLIP);

    ui->btn_cal_minus = lv_button_create(ui->cal_top_layer);
    lv_obj_set_size(ui->btn_cal_minus, 46, 30);
    style_mono_btn(ui->btn_cal_minus);
    lv_obj_set_style_radius(ui->btn_cal_minus, 6, 0);
    lv_obj_add_event_cb(ui->btn_cal_minus, on_cal_minus_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lm = make_label(ui->btn_cal_minus, "-", &zvp_font_zh24, ZVP_COLOR_WHITE);
    lv_obj_center(lm);

    ui->btn_cal_plus = lv_button_create(ui->cal_top_layer);
    lv_obj_set_size(ui->btn_cal_plus, 46, 30);
    style_mono_btn(ui->btn_cal_plus);
    lv_obj_set_style_radius(ui->btn_cal_plus, 6, 0);
    lv_obj_add_event_cb(ui->btn_cal_plus, on_cal_plus_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lp = make_label(ui->btn_cal_plus, "+", &zvp_font_zh24, ZVP_COLOR_WHITE);
    lv_obj_center(lp);

    lv_obj_add_flag(ui->cal_top_layer, LV_OBJ_FLAG_HIDDEN);
}

static void build_cal_fine_layer(zvp_ui_t *ui, lv_obj_t *parent)
{
    /* 右下 1x9 独立全透明参数磁贴条，贴于原厂 52px 黑条上方 (252, 378, 380x44) */
    ui->cal_fine_layer = lv_obj_create(parent);
    lv_obj_remove_style_all(ui->cal_fine_layer);
    lv_obj_set_pos(ui->cal_fine_layer, 252, 378);
    lv_obj_set_size(ui->cal_fine_layer, 380, 44);
    lv_obj_set_style_bg_opa(ui->cal_fine_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(ui->cal_fine_layer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->cal_fine_layer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ui->cal_fine_layer, 2, 0);
    lv_obj_clear_flag(ui->cal_fine_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui->cal_fine_layer, LV_OBJ_FLAG_SCROLLABLE);

    static const char *const k_cal_short[ZVP_CAL_PARAM_COUNT] = {
        "色温", "色调", "饱和", "对比", "伽马", "红", "绿", "蓝"
    };
    for (uint32_t i = 0u; i < ZVP_CAL_PARAM_COUNT; i++) {
        lv_obj_t *tile = lv_button_create(ui->cal_fine_layer);
        lv_obj_set_size(tile, 40, 44);
        style_fine_tile(tile);
        lv_obj_set_style_pad_hor(tile, 0, 0);
        lv_obj_set_style_pad_ver(tile, 2, 0);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(tile, -2, 0);
        lv_obj_add_event_cb(tile, on_cal_param_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        lv_obj_t *lbl_t = make_label(tile, k_cal_short[i], &zvp_font_zh18, ZVP_COLOR_WHITE);
        lv_label_set_long_mode(lbl_t, LV_LABEL_LONG_CLIP);
        ui->cal_param_vals[i] = make_label(tile, "0", &zvp_font_zh18, ZVP_COLOR_WHITE);
        lv_label_set_long_mode(ui->cal_param_vals[i], LV_LABEL_LONG_CLIP);
        ui->cal_param_btns[i] = tile;
    }

    ui->btn_cal_fine_reset = lv_button_create(ui->cal_fine_layer);
    lv_obj_set_size(ui->btn_cal_fine_reset, 42, 44);
    style_fine_tile(ui->btn_cal_fine_reset);
    lv_obj_set_style_pad_hor(ui->btn_cal_fine_reset, 0, 0);
    lv_obj_set_style_pad_ver(ui->btn_cal_fine_reset, 2, 0);
    lv_obj_set_flex_flow(ui->btn_cal_fine_reset, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui->btn_cal_fine_reset, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ui->btn_cal_fine_reset, -2, 0);
    lv_obj_add_event_cb(ui->btn_cal_fine_reset, on_cal_fine_reset_clicked, LV_EVENT_CLICKED, NULL);
    make_label(ui->btn_cal_fine_reset, "\xee\x80\x87", &zvp_font_lut_icons, ZVP_COLOR_WHITE);
    lv_obj_t *lbl_cr = make_label(ui->btn_cal_fine_reset, "重置", &zvp_font_zh18, ZVP_COLOR_WHITE);
    lv_label_set_long_mode(lbl_cr, LV_LABEL_LONG_CLIP);

    lv_obj_add_flag(ui->cal_fine_layer, LV_OBJ_FLAG_HIDDEN);
}

void zvp_ui_build(zvp_ui_t *ui)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!ui || ui->built || !rt) {
        return;
    }

    ui->input_group = lv_group_create();

    /* 全画幅透明根容器：背景完全透传实时取景与原生参数 */
    ui->scr = lv_screen_active();
    lv_obj_set_style_bg_opa(ui->scr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(ui->scr, 0, 0);
    lv_obj_set_scrollbar_mode(ui->scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui->scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui->scr, LV_OBJ_FLAG_CLICKABLE);

    /* 最底层：纵横比实时取景遮罩与构图线画布 (640x480 全屏，无任何交互) */
    build_aspect_overlay(ui, ui->scr);

    /* 左侧一级菜单栏：保持 96x464 紧凑外框，选项扩大至 85x48 防触控误触 */
    ui->left_nav = lv_obj_create(ui->scr);
    lv_obj_set_pos(ui->left_nav, 8, 8);
    lv_obj_set_size(ui->left_nav, 96, 464);
    style_glass_panel(ui->left_nav, 10);
    lv_obj_set_style_pad_hor(ui->left_nav, 3, 0);
    lv_obj_set_style_pad_ver(ui->left_nav, 4, 0);
    lv_obj_set_flex_flow(ui->left_nav, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui->left_nav, 3, 0);
    lv_obj_set_scrollbar_mode(ui->left_nav, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui->left_nav, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui->left_nav, LV_OBJ_FLAG_CLICKABLE);

    /* 可垂直滚动的一级菜单列表容器 */
    ui->nav_list = lv_obj_create(ui->left_nav);
    lv_obj_remove_style_all(ui->nav_list);
    lv_obj_set_size(ui->nav_list, 88, 428);
    lv_obj_set_flex_flow(ui->nav_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui->nav_list, 5, 0);
    lv_obj_set_style_bg_opa(ui->nav_list, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(ui->nav_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui->nav_list, LV_DIR_VER);
    lv_obj_clear_flag(ui->nav_list, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scrollbar_mode(ui->nav_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(ui->nav_list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(ui->nav_list, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(ui->nav_list, lv_color_hex(0xFFFFFF), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(ui->nav_list, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(ui->nav_list, 2, LV_PART_SCROLLBAR);

    static const char *nav_titles[ZVP_PAGE_COUNT] = {
        "滤镜",
        "5G WiFi",
        "共享",
        "快门音",
        "校色",
        "比例",
        "外录",
        "超采",
        "cRAW",
        "设置"
    };
    for (int i = 0; i < ZVP_PAGE_COUNT; ++i) {
        lv_obj_t *btn = lv_button_create(ui->nav_list);
        lv_obj_set_size(btn, 85, 48);
        style_mono_btn(btn);
        lv_obj_set_style_pad_hor(btn, 2, 0);
        lv_obj_set_style_pad_ver(btn, 4, 0);
        lv_obj_add_event_cb(btn, on_nav_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *lbl = make_label(btn, nav_titles[i], &zvp_font_zh20, ZVP_COLOR_WHITE);
        lv_obj_center(lbl);
        ui->nav_btns[i] = btn;
    }

    ui->lbl_nav_hint = make_label(ui->left_nav, "C1 开/关", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);
    lv_obj_set_width(ui->lbl_nav_hint, 88);
    lv_obj_set_style_text_align(ui->lbl_nav_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(ui->lbl_nav_hint, 1, 0);

    /* 右侧二级操作区：非LUT页为 522x464，LUT页极限压窄至 92x464 留出右半屏透明取景 */
    ui->right_panel = lv_obj_create(ui->scr);
    lv_obj_set_pos(ui->right_panel, 110, 8);
    lv_obj_set_size(ui->right_panel, 522, 464);
    style_glass_panel(ui->right_panel, 10);
    lv_obj_set_style_pad_all(ui->right_panel, 10, 0);
    lv_obj_set_scrollbar_mode(ui->right_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui->right_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui->right_panel, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < ZVP_PAGE_COUNT; ++i) {
        lv_obj_t *p = lv_obj_create(ui->right_panel);
        lv_obj_remove_style_all(p);
        lv_obj_set_pos(p, 0, 0);
        if (i == ZVP_PAGE_LUT) {
            lv_obj_set_size(p, 98, 408);
        } else if (i == ZVP_PAGE_COLOR_CAL) {
            lv_obj_set_size(p, 124, 452);
        } else {
            lv_obj_set_size(p, 502, 444);
            lv_obj_set_style_pad_left(p, 36, 0);
        }
        lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
        ui->pages[i] = p;
    }

    /* 共享 / 外录 / 超采 暂无功能，二级菜单保持留空 */
    build_lut_page(ui, ui->pages[ZVP_PAGE_LUT]);
    build_wifi5g_page(ui, ui->pages[ZVP_PAGE_WIFI5G]);
    build_custom_shutter_page(ui, ui->pages[ZVP_PAGE_CUSTOM_SHUTTER]);
    build_color_cal_page(ui, ui->pages[ZVP_PAGE_COLOR_CAL]);
    build_aspect_page(ui, ui->pages[ZVP_PAGE_ASPECT_RATIO]);
    build_craw_page(ui, ui->pages[ZVP_PAGE_2K_CRAW]);
    build_settings_page(ui, ui->pages[ZVP_PAGE_SETTINGS]);
    build_mini_widget(ui, ui->scr);
    build_lut_top_layer(ui, ui->scr);
    build_lut_fine_layer(ui, ui->scr);
    build_cal_top_layer(ui, ui->scr);
    build_cal_fine_layer(ui, ui->scr);

    ui->built = true;
    zvp_ui_switch_page(ui, ZVP_PAGE_LUT);
    set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_LUT]);
    zvp_ui_refresh(ui);
}

void zvp_ui_switch_page(zvp_ui_t *ui, zvp_ui_page_t page)
{
    if (!ui || !ui->built || page >= ZVP_PAGE_COUNT) {
        return;
    }
    ui->active_page = page;
    if (ui->right_panel) {
        if (page == ZVP_PAGE_LUT) {
            lv_obj_set_pos(ui->right_panel, 110, 54);
            lv_obj_set_size(ui->right_panel, 108, 418);
            lv_obj_set_style_pad_all(ui->right_panel, 4, 0);
            zvp_runtime_t *rt = zvp_runtime_get();
            if (rt) {
                if (rt->lut.active_sel > 0u && rt->lut.active_sel < rt->lut.num_slots) {
                    ui->lut_cat = lut_slot_category(&rt->lut, rt->lut.active_sel);
                }
                lut_rebuild_visible_rows(ui, &rt->lut);
            }
        } else if (page == ZVP_PAGE_COLOR_CAL) {
            lv_obj_set_pos(ui->right_panel, 110, 8);
            lv_obj_set_size(ui->right_panel, 136, 464);
            lv_obj_set_style_pad_all(ui->right_panel, 6, 0);
        } else {
            lv_obj_set_pos(ui->right_panel, 110, 8);
            lv_obj_set_size(ui->right_panel, 522, 464);
            lv_obj_set_style_pad_all(ui->right_panel, 10, 0);
        }
    }
    if (page == ZVP_PAGE_CUSTOM_SHUTTER) {
        zvp_runtime_t *rt = zvp_runtime_get();
        if (rt && rt->sound.count <= 2u) {
            zvp_sound_scan(&rt->sound);
            zvp_ui_refresh(ui);
        }
    }
    for (int i = 0; i < ZVP_PAGE_COUNT; ++i) {
        if (i == (int)page) {
            lv_obj_add_state(ui->nav_btns[i], LV_STATE_CHECKED);
            lv_obj_t *lbl = lv_obj_get_child(ui->nav_btns[i], 0);
            if (lbl) lv_obj_set_style_text_color(lbl, ZVP_COLOR_BG_BLACK, 0);
            lv_obj_clear_flag(ui->pages[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_state(ui->nav_btns[i], LV_STATE_CHECKED);
            lv_obj_t *lbl = lv_obj_get_child(ui->nav_btns[i], 0);
            if (lbl) lv_obj_set_style_text_color(lbl, ZVP_COLOR_WHITE, 0);
            lv_obj_add_flag(ui->pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    zvp_ui_sync_overlay(ui);
}

/* 清晰直观的左右分栏 + 二维信道网格导航 */
bool zvp_ui_handle_nav_key(zvp_ui_t *ui, uint32_t act_mask)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!ui || !ui->built || !rt) {
        return false;
    }

    lv_obj_t *cur = ui->focused_obj;

    /* 0. 焦点位于单行 1x8 全透明三级微调条 (←/→切项, ↑/↓/拨轮直接调值, OK保存/重置) */
    if (ui->active_page == ZVP_PAGE_LUT && rt->lut.active_sel > 0u) {
        lv_obj_t *fine_row[8] = {
            ui->lut_param_btns[0], ui->lut_param_btns[1], ui->lut_param_btns[2], ui->lut_param_btns[3],
            ui->lut_param_btns[4], ui->lut_param_btns[5], ui->lut_param_btns[6], ui->btn_lut_fine_reset
        };
        int fidx = -1;
        for (int k = 0; k < 8; k++) {
            if (cur == fine_row[k]) {
                fidx = k;
                break;
            }
        }
        if (fidx >= 0) {
            if (act_mask & ZVP_ACT_NAV_LEFT) {
                if (fidx == 0) {
                    /* 最左格往左直接无缝回到二级列表当前高亮 LUT 行 */
                    lut_focus_slot_or_cat(ui, &rt->lut);
                    zvp_ui_refresh(ui);
                } else {
                    int prev = fidx - 1;
                    if (prev < (int)ZVP_LUT_PARAM_COUNT) ui->lut_fine_param = (zvp_lut_param_t)prev;
                    set_focus_obj(ui, fine_row[prev]);
                    zvp_ui_refresh(ui);
                }
                return true;
            }
            if (act_mask & ZVP_ACT_NAV_RIGHT) {
                if (fidx < 7) {
                    int next = fidx + 1;
                    if (next < (int)ZVP_LUT_PARAM_COUNT) ui->lut_fine_param = (zvp_lut_param_t)next;
                    set_focus_obj(ui, fine_row[next]);
                    zvp_ui_refresh(ui);
                }
                return true;
            }
            if (act_mask & (ZVP_ACT_NAV_UP | ZVP_ACT_DIAL_NEXT)) {
                if (fidx < (int)ZVP_LUT_PARAM_COUNT) {
                    ui->lut_fine_param = (zvp_lut_param_t)fidx;
                    trigger_lut_fine_step(rt, 1, "fine_inc");
                }
                return true;
            }
            if (act_mask & (ZVP_ACT_NAV_DOWN | ZVP_ACT_DIAL_PREV)) {
                if (fidx < (int)ZVP_LUT_PARAM_COUNT) {
                    ui->lut_fine_param = (zvp_lut_param_t)fidx;
                    trigger_lut_fine_step(rt, -1, "fine_dec");
                }
                return true;
            }
            if (act_mask & ZVP_ACT_NAV_ENTER) {
                if (fidx == 7) {
                    trigger_lut_fine_reset(rt, "key_reset");
                } else {
                    (void)zvp_lut_flush_pending_cfg(&rt->lut);
                    rt->lut_cfg_dirty_ms = 0u;
                    zvp_ui_refresh(ui);
                }
                return true;
            }
            return true;
        }
    }

    if (!cur) {
        cur = ui->nav_btns[ui->active_page];
        set_focus_obj(ui, cur);
    }

    /* 1. 焦点位于左侧一级菜单栏 */
    for (int i = 0; i < ZVP_PAGE_COUNT; ++i) {
        if (cur == ui->nav_btns[i]) {
            if (act_mask & (ZVP_ACT_NAV_UP | ZVP_ACT_DIAL_PREV)) {
                int next = (i + ZVP_PAGE_COUNT - 1) % ZVP_PAGE_COUNT;
                zvp_ui_switch_page(ui, (zvp_ui_page_t)next);
                set_focus_obj(ui, ui->nav_btns[next]);
                return true;
            }
            if (act_mask & (ZVP_ACT_NAV_DOWN | ZVP_ACT_DIAL_NEXT)) {
                int next = (i + 1) % ZVP_PAGE_COUNT;
                zvp_ui_switch_page(ui, (zvp_ui_page_t)next);
                set_focus_obj(ui, ui->nav_btns[next]);
                return true;
            }
            if (act_mask & (ZVP_ACT_NAV_RIGHT | ZVP_ACT_NAV_ENTER)) {
                zvp_ui_switch_page(ui, (zvp_ui_page_t)i);
                lv_obj_t *first = NULL;
                if (i == ZVP_PAGE_LUT) {
                    lut_focus_slot_or_cat(ui, &rt->lut);
                    zvp_ui_refresh(ui);
                    return true;
                } else if (i == ZVP_PAGE_WIFI5G) {
                    first = ui->btn_wifi_card;
                } else if (i == ZVP_PAGE_CUSTOM_SHUTTER) {
                    first = ui->btn_shutter_enable;
                } else if (i == ZVP_PAGE_COLOR_CAL) {
                    first = ui->btn_cal_enable;
                } else if (i == ZVP_PAGE_ASPECT_RATIO) {
                    first = ui->btn_aspect_enable;
                } else if (i == ZVP_PAGE_2K_CRAW) {
                    first = ui->btn_craw_mode;
                } else if (i == ZVP_PAGE_SETTINGS) {
                    first = ui->btn_c1_mode;
                }
                if (first) {
                    set_focus_obj(ui, first);
                }
                return true;
            }
            return true;
        }
    }

    /* 1.5 焦点位于中栏 Page 1 (上方独立横栏 [关闭|索尼|富士|理光|SD卡导入] + 下方窄列表/SD包列表) */
    if (ui->active_page == ZVP_PAGE_LUT) {
        lut_rebuild_visible_rows(ui, &rt->lut);
        uint32_t vis = ui->lut_sd_mode ? lut_sd_visible_rows(ui) : ui->lut_visible_count;
        uint32_t cat_idx = (ui->lut_cat < ZVP_LUT_CAT_COUNT) ? ui->lut_cat : 0u;
        lv_obj_t *top_btns[5] = {
            ui->btn_lut_off, ui->lut_cat_btns[0], ui->lut_cat_btns[1], ui->lut_cat_btns[2], ui->btn_lut_sd
        };

        for (int t = 0; t < 5; t++) {
            if (cur == top_btns[t]) {
                if (act_mask & ZVP_ACT_NAV_LEFT) {
                    if (t == 0) {
                        ui->lut_sd_mode = false;
                        set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_LUT]);
                        zvp_ui_sync_overlay(ui);
                    } else {
                        int prev = t - 1;
                        if (prev >= 1 && prev <= 3) {
                            ui->lut_sd_mode = false;
                            ui->lut_cat = (uint8_t)(prev - 1);
                            lut_rebuild_visible_rows(ui, &rt->lut);
                            zvp_ui_sync_overlay(ui);
                        }
                        set_focus_obj(ui, top_btns[prev]);
                    }
                } else if (act_mask & ZVP_ACT_NAV_RIGHT) {
                    if (t < 4) {
                        int next = t + 1;
                        if (next >= 1 && next <= 3) {
                            ui->lut_sd_mode = false;
                            ui->lut_cat = (uint8_t)(next - 1);
                            lut_rebuild_visible_rows(ui, &rt->lut);
                            zvp_ui_sync_overlay(ui);
                        }
                        set_focus_obj(ui, top_btns[next]);
                    }
                } else if (act_mask & (ZVP_ACT_NAV_DOWN | ZVP_ACT_DIAL_NEXT)) {
                    if (t == 4 && !ui->lut_sd_mode) {
                        trigger_lut_enter_sd_mode(rt, "key_sd_down");
                    } else if (ui->lut_sd_mode) {
                        if (vis > 0u) set_focus_obj(ui, ui->lut_row_btns[0]);
                    } else if (vis > 0u) {
                        int act_row = lut_find_row_for_slot(ui, rt->lut.active_sel);
                        if (act_row >= 0) {
                            set_focus_obj(ui, ui->lut_row_btns[act_row]);
                        } else {
                            set_focus_obj(ui, ui->lut_row_btns[0]);
                            trigger_lut_preview_slot(rt, ui->lut_row_slots[0], false, "top_down");
                            zvp_ui_refresh(ui);
                        }
                    }
                } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                    if (t == 0) {
                        ui->lut_sd_mode = false;
                        trigger_lut_preview_slot(rt, 0u, true, "key_off");
                        zvp_ui_sync_overlay(ui);
                    } else if (t == 4) {
                        trigger_lut_enter_sd_mode(rt, "key_sd_enter");
                    } else if (vis > 0u) {
                        ui->lut_sd_mode = false;
                        zvp_ui_sync_overlay(ui);
                        int act_row = lut_find_row_for_slot(ui, rt->lut.active_sel);
                        if (act_row >= 0) {
                            set_focus_obj(ui, ui->lut_row_btns[act_row]);
                        } else {
                            set_focus_obj(ui, ui->lut_row_btns[0]);
                            trigger_lut_preview_slot(rt, ui->lut_row_slots[0], false, "cat_enter");
                            zvp_ui_refresh(ui);
                        }
                    }
                }
                return true;
            }
        }

        for (uint32_t r = 0u; r < vis; r++) {
            if (cur == ui->lut_row_btns[r]) {
                if (act_mask & ZVP_ACT_NAV_LEFT) {
                    if (ui->lut_sd_mode) {
                        ui->lut_sd_mode = false;
                        zvp_ui_sync_overlay(ui);
                        set_focus_obj(ui, ui->btn_lut_sd);
                    } else {
                        (void)zvp_lut_flush_pending_cfg(&rt->lut);
                        rt->lut_cfg_dirty_ms = 0u;
                        set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_LUT]);
                    }
                    return true;
                }
                if (ui->lut_sd_mode) {
                    if (act_mask & ZVP_ACT_NAV_UP) {
                        if (r == 0u) set_focus_obj(ui, ui->btn_lut_sd);
                        else set_focus_obj(ui, ui->lut_row_btns[r - 1u]);
                    } else if (act_mask & ZVP_ACT_DIAL_PREV) {
                        set_focus_obj(ui, ui->lut_row_btns[(r + vis - 1u) % vis]);
                    } else if (act_mask & (ZVP_ACT_NAV_DOWN | ZVP_ACT_DIAL_NEXT)) {
                        set_focus_obj(ui, ui->lut_row_btns[(r + 1u) % vis]);
                    } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                        trigger_lut_sd_row_activate(rt, r, "key_sd_row");
                    }
                    zvp_ui_refresh(ui);
                    return true;
                }
                if (act_mask & ZVP_ACT_NAV_UP) {
                    if (r == 0u) {
                        set_focus_obj(ui, ui->lut_cat_btns[cat_idx]);
                    } else {
                        set_focus_obj(ui, ui->lut_row_btns[r - 1u]);
                        trigger_lut_preview_slot(rt, ui->lut_row_slots[r - 1u], false, "scroll_up");
                        zvp_ui_refresh(ui);
                    }
                    return true;
                }
                if (act_mask & ZVP_ACT_DIAL_PREV) {
                    uint32_t next = (r + vis - 1u) % vis;
                    set_focus_obj(ui, ui->lut_row_btns[next]);
                    trigger_lut_preview_slot(rt, ui->lut_row_slots[next], false, "dial_prev");
                    zvp_ui_refresh(ui);
                    return true;
                }
                if (act_mask & (ZVP_ACT_NAV_DOWN | ZVP_ACT_DIAL_NEXT)) {
                    uint32_t next = (r + 1u) % vis;
                    set_focus_obj(ui, ui->lut_row_btns[next]);
                    trigger_lut_preview_slot(rt, ui->lut_row_slots[next], false, "scroll_down");
                    zvp_ui_refresh(ui);
                    return true;
                }
                if (act_mask & (ZVP_ACT_NAV_RIGHT | ZVP_ACT_NAV_ENTER)) {
                    if (rt->lut.active_sel > 0u) {
                        if ((uint32_t)ui->lut_fine_param >= ZVP_LUT_PARAM_COUNT) {
                            ui->lut_fine_param = ZVP_LUT_PARAM_STRENGTH;
                        }
                        set_focus_obj(ui, ui->lut_param_btns[ui->lut_fine_param]);
                        zvp_ui_refresh(ui);
                    } else {
                        trigger_lut_preview_slot(rt, ui->lut_row_slots[r], true, "key_apply");
                        zvp_ui_refresh(ui);
                    }
                    return true;
                }
                return true;
            }
        }

        lut_focus_slot_or_cat(ui, &rt->lut);
        return true;
    }

    /* 2. 焦点位于右侧 Page 0 (5GHz Wi-Fi) */
    if (ui->active_page == ZVP_PAGE_WIFI5G) {
        if (cur == ui->btn_wifi_card) {
            if (act_mask & ZVP_ACT_NAV_LEFT) {
                set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_WIFI5G]);
            } else if (act_mask & (ZVP_ACT_NAV_DOWN | ZVP_ACT_DIAL_NEXT)) {
                set_focus_obj(ui, ui->ch_btns[0]);
            } else if (act_mask & ZVP_ACT_DIAL_PREV) {
                set_focus_obj(ui, ui->ch_btns[ZVP_WIFI5G_CHANNEL_COUNT - 1]);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_wifi5g_toggle(rt, "key");
            }
            return true;
        }

        for (size_t i = 0; i < ZVP_WIFI5G_CHANNEL_COUNT; ++i) {
            if (cur == ui->ch_btns[i]) {
                if (act_mask & ZVP_ACT_NAV_ENTER) {
                    size_t cnt = 0;
                    const uint8_t *chs = zvp_wifi5g_get_channels(&cnt);
                    if (i < cnt) {
                        trigger_channel_select(rt, chs[i], "key");
                    }
                } else if (act_mask & ZVP_ACT_DIAL_NEXT) {
                    if (i + 1 < ZVP_WIFI5G_CHANNEL_COUNT) {
                        set_focus_obj(ui, ui->ch_btns[i + 1]);
                    } else {
                        set_focus_obj(ui, ui->btn_wifi_card);
                    }
                } else if (act_mask & ZVP_ACT_DIAL_PREV) {
                    if (i == 0) {
                        set_focus_obj(ui, ui->btn_wifi_card);
                    } else {
                        set_focus_obj(ui, ui->ch_btns[i - 1]);
                    }
                } else if (act_mask & ZVP_ACT_NAV_LEFT) {
                    if (i == 0 || i == 5) {
                        set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_WIFI5G]);
                    } else {
                        set_focus_obj(ui, ui->ch_btns[i - 1]);
                    }
                } else if (act_mask & ZVP_ACT_NAV_RIGHT) {
                    if (i + 1 < ZVP_WIFI5G_CHANNEL_COUNT) {
                        set_focus_obj(ui, ui->ch_btns[i + 1]);
                    }
                } else if (act_mask & ZVP_ACT_NAV_UP) {
                    if (i < 5) {
                        set_focus_obj(ui, ui->btn_wifi_card);
                    } else {
                        set_focus_obj(ui, ui->ch_btns[i - 5]);
                    }
                } else if (act_mask & ZVP_ACT_NAV_DOWN) {
                    if (i + 5 < ZVP_WIFI5G_CHANNEL_COUNT) {
                        set_focus_obj(ui, ui->ch_btns[i + 5]);
                    }
                }
                return true;
            }
        }

        set_focus_obj(ui, ui->btn_wifi_card);
        return true;
    }

    /* 3. 焦点位于右侧 Page (自定义快门音) */
    if (ui->active_page == ZVP_PAGE_CUSTOM_SHUTTER) {
        if (cur == ui->btn_shutter_enable) {
            if (act_mask & ZVP_ACT_NAV_LEFT) {
                set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_CUSTOM_SHUTTER]);
            } else if (act_mask & (ZVP_ACT_NAV_DOWN | ZVP_ACT_DIAL_NEXT)) {
                set_focus_obj(ui, ui->btn_mini_enable);
            } else if (act_mask & ZVP_ACT_DIAL_PREV) {
                set_focus_obj(ui, ui->btn_sound_test);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_shutter_sound_toggle(rt, "key");
            }
            return true;
        }
        if (cur == ui->btn_mini_enable) {
            if (act_mask & ZVP_ACT_NAV_LEFT) {
                set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_CUSTOM_SHUTTER]);
            } else if (act_mask & (ZVP_ACT_NAV_UP | ZVP_ACT_DIAL_PREV)) {
                set_focus_obj(ui, ui->btn_shutter_enable);
            } else if (act_mask & (ZVP_ACT_NAV_DOWN | ZVP_ACT_DIAL_NEXT)) {
                set_focus_obj(ui, ui->btn_sound_prev);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_mini_widget_toggle(rt, "key");
            }
            return true;
        }
        if (cur == ui->btn_sound_prev) {
            if (act_mask & ZVP_ACT_NAV_LEFT) {
                set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_CUSTOM_SHUTTER]);
            } else if (act_mask & (ZVP_ACT_NAV_RIGHT | ZVP_ACT_DIAL_NEXT)) {
                set_focus_obj(ui, ui->btn_sound_next);
            } else if (act_mask & (ZVP_ACT_NAV_UP | ZVP_ACT_DIAL_PREV)) {
                set_focus_obj(ui, ui->btn_mini_enable);
            } else if (act_mask & ZVP_ACT_NAV_DOWN) {
                set_focus_obj(ui, ui->btn_sound_test);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_sound_step(rt, -1, "key");
            }
            return true;
        }
        if (cur == ui->btn_sound_next) {
            if (act_mask & (ZVP_ACT_NAV_LEFT | ZVP_ACT_DIAL_PREV)) {
                set_focus_obj(ui, ui->btn_sound_prev);
            } else if (act_mask & ZVP_ACT_NAV_UP) {
                set_focus_obj(ui, ui->btn_mini_enable);
            } else if (act_mask & (ZVP_ACT_NAV_DOWN | ZVP_ACT_DIAL_NEXT)) {
                set_focus_obj(ui, ui->btn_sound_test);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_sound_step(rt, 1, "key");
            }
            return true;
        }
        if (cur == ui->btn_sound_test) {
            if (act_mask & ZVP_ACT_NAV_LEFT) {
                set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_CUSTOM_SHUTTER]);
            } else if (act_mask & (ZVP_ACT_NAV_UP | ZVP_ACT_DIAL_PREV)) {
                set_focus_obj(ui, ui->btn_sound_next);
            } else if (act_mask & ZVP_ACT_DIAL_NEXT) {
                set_focus_obj(ui, ui->btn_shutter_enable);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_sound_play(rt, "key");
            }
            return true;
        }

        set_focus_obj(ui, ui->btn_shutter_enable);
        return true;
    }

    /* 4. 焦点位于右侧 Page (纵横比) */
    if (ui->active_page == ZVP_PAGE_ASPECT_RATIO) {
        if (cur == ui->btn_aspect_enable) {
            if (act_mask & ZVP_ACT_NAV_LEFT) {
                set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_ASPECT_RATIO]);
            } else if (act_mask & (ZVP_ACT_NAV_DOWN | ZVP_ACT_DIAL_NEXT)) {
                set_focus_obj(ui, ui->aspect_btns[0]);
            } else if (act_mask & ZVP_ACT_DIAL_PREV) {
                set_focus_obj(ui, ui->btn_aspect_par);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_aspect_toggle(rt, "key");
            }
            return true;
        }

        for (size_t i = 0; i < ZVP_ASPECT_PRESET_COUNT; ++i) {
            if (cur == ui->aspect_btns[i]) {
                if (act_mask & ZVP_ACT_NAV_ENTER) {
                    trigger_aspect_preset(rt, (uint8_t)i, "key");
                } else if (act_mask & ZVP_ACT_DIAL_NEXT) {
                    if (i + 1u < ZVP_ASPECT_PRESET_COUNT) {
                        set_focus_obj(ui, ui->aspect_btns[i + 1u]);
                    } else {
                        set_focus_obj(ui, ui->btn_aspect_style);
                    }
                } else if (act_mask & ZVP_ACT_DIAL_PREV) {
                    if (i == 0u) {
                        set_focus_obj(ui, ui->btn_aspect_enable);
                    } else {
                        set_focus_obj(ui, ui->aspect_btns[i - 1u]);
                    }
                } else if (act_mask & ZVP_ACT_NAV_LEFT) {
                    if ((i % 4u) == 0u) {
                        set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_ASPECT_RATIO]);
                    } else {
                        set_focus_obj(ui, ui->aspect_btns[i - 1u]);
                    }
                } else if (act_mask & ZVP_ACT_NAV_RIGHT) {
                    if ((i % 4u) < 3u && (i + 1u) < ZVP_ASPECT_PRESET_COUNT) {
                        set_focus_obj(ui, ui->aspect_btns[i + 1u]);
                    }
                } else if (act_mask & ZVP_ACT_NAV_UP) {
                    if (i < 4u) {
                        set_focus_obj(ui, ui->btn_aspect_enable);
                    } else {
                        set_focus_obj(ui, ui->aspect_btns[i - 4u]);
                    }
                } else if (act_mask & ZVP_ACT_NAV_DOWN) {
                    if (i + 4u < ZVP_ASPECT_PRESET_COUNT) {
                        set_focus_obj(ui, ui->aspect_btns[i + 4u]);
                    } else {
                        size_t col = i % 4u;
                        lv_obj_t *dst = (col == 0u) ? ui->btn_aspect_style
                                                    : ((col <= 2u) ? ui->btn_aspect_guide : ui->btn_aspect_par);
                        set_focus_obj(ui, dst);
                    }
                }
                return true;
            }
        }

        if (cur == ui->btn_aspect_style) {
            if (act_mask & ZVP_ACT_NAV_LEFT) {
                set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_ASPECT_RATIO]);
            } else if (act_mask & (ZVP_ACT_NAV_RIGHT | ZVP_ACT_DIAL_NEXT)) {
                set_focus_obj(ui, ui->btn_aspect_guide);
            } else if (act_mask & ZVP_ACT_DIAL_PREV) {
                set_focus_obj(ui, ui->aspect_btns[ZVP_ASPECT_PRESET_COUNT - 1u]);
            } else if (act_mask & ZVP_ACT_NAV_UP) {
                set_focus_obj(ui, ui->aspect_btns[8]);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_aspect_style_cycle(rt, 1, "key");
            }
            return true;
        }

        if (cur == ui->btn_aspect_guide) {
            if (act_mask & (ZVP_ACT_NAV_LEFT | ZVP_ACT_DIAL_PREV)) {
                set_focus_obj(ui, ui->btn_aspect_style);
            } else if (act_mask & (ZVP_ACT_NAV_RIGHT | ZVP_ACT_DIAL_NEXT)) {
                set_focus_obj(ui, ui->btn_aspect_par);
            } else if (act_mask & ZVP_ACT_NAV_UP) {
                set_focus_obj(ui, ui->aspect_btns[9]);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_aspect_guide_cycle(rt, 1, "key");
            }
            return true;
        }

        if (cur == ui->btn_aspect_par) {
            if (act_mask & (ZVP_ACT_NAV_LEFT | ZVP_ACT_DIAL_PREV)) {
                set_focus_obj(ui, ui->btn_aspect_guide);
            } else if (act_mask & ZVP_ACT_DIAL_NEXT) {
                set_focus_obj(ui, ui->btn_aspect_enable);
            } else if (act_mask & ZVP_ACT_NAV_UP) {
                set_focus_obj(ui, ui->aspect_btns[11]);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_aspect_par_cycle(rt, 1, "key");
            }
            return true;
        }

        set_focus_obj(ui, ui->btn_aspect_enable);
        return true;
    }

    /* 5. 焦点位于右侧 Page (设置与安全) */
    if (ui->active_page == ZVP_PAGE_SETTINGS) {
        if (cur == ui->btn_c1_mode) {
            if (act_mask & ZVP_ACT_NAV_LEFT) {
                set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_SETTINGS]);
            } else if (act_mask & (ZVP_ACT_NAV_DOWN | ZVP_ACT_DIAL_NEXT)) {
                set_focus_obj(ui, ui->btn_flip_card);
            } else if (act_mask & ZVP_ACT_DIAL_PREV) {
                set_focus_obj(ui, ui->btn_recovery);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_c1_mode_toggle(rt, "key");
            }
            return true;
        }
        if (cur == ui->btn_flip_card) {
            if (act_mask & ZVP_ACT_NAV_LEFT) {
                set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_SETTINGS]);
            } else if (act_mask & (ZVP_ACT_NAV_UP | ZVP_ACT_DIAL_PREV)) {
                set_focus_obj(ui, ui->btn_c1_mode);
            } else if (act_mask & (ZVP_ACT_NAV_DOWN | ZVP_ACT_DIAL_NEXT)) {
                set_focus_obj(ui, ui->btn_native_touch);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_auto_flip_toggle(rt, "key");
            }
            return true;
        }
        if (cur == ui->btn_native_touch) {
            if (act_mask & ZVP_ACT_NAV_LEFT) {
                set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_SETTINGS]);
            } else if (act_mask & (ZVP_ACT_NAV_UP | ZVP_ACT_DIAL_PREV)) {
                set_focus_obj(ui, ui->btn_flip_card);
            } else if (act_mask & (ZVP_ACT_NAV_DOWN | ZVP_ACT_DIAL_NEXT)) {
                set_focus_obj(ui, ui->btn_recovery);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_native_touch_toggle(rt, "key");
            }
            return true;
        }
        if (cur == ui->btn_recovery) {
            if (act_mask & ZVP_ACT_NAV_LEFT) {
                set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_SETTINGS]);
            } else if (act_mask & (ZVP_ACT_NAV_UP | ZVP_ACT_DIAL_PREV)) {
                set_focus_obj(ui, ui->btn_native_touch);
            } else if (act_mask & ZVP_ACT_DIAL_NEXT) {
                set_focus_obj(ui, ui->btn_c1_mode);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_recovery(rt, "key");
            }
            return true;
        }

        set_focus_obj(ui, ui->btn_c1_mode);
        return true;
    }

    /* 4.5 焦点位于屏幕校色页（左副栏 4 按钮 <-> 右下 1x9 参数微调磁贴条） */
    if (ui->active_page == ZVP_PAGE_COLOR_CAL) {
        lv_obj_t *side_btns[4] = {
            ui->btn_cal_enable, ui->btn_cal_compare, ui->btn_cal_preset, ui->btn_cal_reset
        };
        for (int s = 0; s < 4; s++) {
            if (cur == side_btns[s]) {
                if (act_mask & ZVP_ACT_NAV_LEFT) {
                    set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_COLOR_CAL]);
                } else if (act_mask & ZVP_ACT_NAV_RIGHT) {
                    uint32_t p = ((uint32_t)ui->cal_sel_param < ZVP_CAL_PARAM_COUNT)
                                     ? (uint32_t)ui->cal_sel_param
                                     : 0u;
                    ui->cal_sel_param = (zvp_cal_param_t)p;
                    if (ui->cal_param_btns[p]) {
                        set_focus_obj(ui, ui->cal_param_btns[p]);
                    }
                    zvp_ui_refresh(ui);
                } else if (act_mask & (ZVP_ACT_NAV_UP | ZVP_ACT_DIAL_PREV)) {
                    set_focus_obj(ui, side_btns[(s + 3) % 4]);
                } else if (act_mask & (ZVP_ACT_NAV_DOWN | ZVP_ACT_DIAL_NEXT)) {
                    set_focus_obj(ui, side_btns[(s + 1) % 4]);
                } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                    if (s == 0) trigger_cal_toggle_enable(rt, "key");
                    else if (s == 1) trigger_cal_toggle_compare(rt, "key");
                    else if (s == 2) trigger_cal_cycle_preset(rt, 1, "key");
                    else trigger_cal_reset(rt, "key");
                }
                return true;
            }
        }

        lv_obj_t *fine_row[9] = {
            ui->cal_param_btns[0], ui->cal_param_btns[1], ui->cal_param_btns[2],
            ui->cal_param_btns[3], ui->cal_param_btns[4], ui->cal_param_btns[5],
            ui->cal_param_btns[6], ui->cal_param_btns[7], ui->btn_cal_fine_reset
        };
        for (int f = 0; f < 9; f++) {
            if (cur == fine_row[f]) {
                if (act_mask & ZVP_ACT_NAV_LEFT) {
                    if (f == 0) {
                        set_focus_obj(ui, ui->btn_cal_enable);
                    } else {
                        int prev = f - 1;
                        if (prev < (int)ZVP_CAL_PARAM_COUNT) {
                            ui->cal_sel_param = (zvp_cal_param_t)prev;
                        }
                        set_focus_obj(ui, fine_row[prev]);
                        zvp_ui_refresh(ui);
                    }
                } else if (act_mask & ZVP_ACT_NAV_RIGHT) {
                    if (f < 8) {
                        int next = f + 1;
                        if (next < (int)ZVP_CAL_PARAM_COUNT) {
                            ui->cal_sel_param = (zvp_cal_param_t)next;
                        }
                        set_focus_obj(ui, fine_row[next]);
                        zvp_ui_refresh(ui);
                    }
                } else if (act_mask & (ZVP_ACT_NAV_UP | ZVP_ACT_DIAL_NEXT)) {
                    if (f < (int)ZVP_CAL_PARAM_COUNT) {
                        ui->cal_sel_param = (zvp_cal_param_t)f;
                        trigger_cal_step_param(rt, 1, "key_inc");
                    }
                } else if (act_mask & (ZVP_ACT_NAV_DOWN | ZVP_ACT_DIAL_PREV)) {
                    if (f < (int)ZVP_CAL_PARAM_COUNT) {
                        ui->cal_sel_param = (zvp_cal_param_t)f;
                        trigger_cal_step_param(rt, -1, "key_dec");
                    }
                } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                    if (f == 8) {
                        trigger_cal_reset(rt, "key_fine_reset");
                    } else {
                        /* 调参过程中按 OK 键即刻一键对比原色，再按恢复，提升盲操校色效率 */
                        trigger_cal_toggle_compare(rt, "key_fine_cmp");
                    }
                }
                return true;
            }
        }

        set_focus_obj(ui, ui->btn_cal_enable);
        return true;
    }

    /* 5. 焦点位于右侧 Page 9 (2K cRAW 纯内存录制) */
    if (ui->active_page == ZVP_PAGE_2K_CRAW) {
        if (cur == ui->btn_craw_mode) {
            if (act_mask & ZVP_ACT_NAV_LEFT) {
                set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_2K_CRAW]);
            } else if (act_mask & (ZVP_ACT_NAV_DOWN | ZVP_ACT_DIAL_NEXT)) {
                set_focus_obj(ui, ui->btn_craw_pool);
            } else if (act_mask & ZVP_ACT_DIAL_PREV) {
                set_focus_obj(ui, ui->btn_craw_uninstall);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_craw_cycle_mode(rt, "key");
            }
            return true;
        }
        if (cur == ui->btn_craw_pool) {
            if (act_mask & ZVP_ACT_NAV_LEFT) {
                set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_2K_CRAW]);
            } else if (act_mask & (ZVP_ACT_NAV_RIGHT | ZVP_ACT_DIAL_NEXT)) {
                set_focus_obj(ui, ui->btn_craw_snap);
            } else if (act_mask & (ZVP_ACT_NAV_UP | ZVP_ACT_DIAL_PREV)) {
                set_focus_obj(ui, ui->btn_craw_mode);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_craw_pool_toggle(rt, "key");
            }
            return true;
        }
        if (cur == ui->btn_craw_snap) {
            if (act_mask & (ZVP_ACT_NAV_LEFT | ZVP_ACT_DIAL_PREV)) {
                set_focus_obj(ui, ui->btn_craw_pool);
            } else if (act_mask & (ZVP_ACT_NAV_RIGHT | ZVP_ACT_DIAL_NEXT)) {
                set_focus_obj(ui, ui->btn_craw_uninstall);
            } else if (act_mask & ZVP_ACT_NAV_UP) {
                set_focus_obj(ui, ui->btn_craw_mode);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_craw_snap(rt, "key");
            }
            return true;
        }
        if (cur == ui->btn_craw_uninstall) {
            if (act_mask & (ZVP_ACT_NAV_LEFT | ZVP_ACT_DIAL_PREV)) {
                set_focus_obj(ui, ui->btn_craw_snap);
            } else if (act_mask & ZVP_ACT_DIAL_NEXT) {
                set_focus_obj(ui, ui->btn_craw_mode);
            } else if (act_mask & ZVP_ACT_NAV_UP) {
                set_focus_obj(ui, ui->btn_craw_mode);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_craw_uninstall(rt, "key");
            }
            return true;
        }

        set_focus_obj(ui, ui->btn_craw_mode);
        return true;
    }

    return false;
}

void zvp_ui_refresh(zvp_ui_t *ui)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!ui || !ui->built || !rt) {
        return;
    }

    /* 自定义 LUT 顶栏开关、品牌分类页签、窄列表 & 微调浮块更新 */
    if (ui->lut_list) {
        uint32_t nslots = (rt->lut.num_slots > 0u && rt->lut.num_slots <= ZVP_LUT_NUM_SLOTS)
                              ? rt->lut.num_slots
                              : 1u;
        uint32_t cur_sel = (rt->lut.active_sel < nslots) ? rt->lut.active_sel : 0u;

        if (ui->btn_lut_off) {
            set_badge_checked(ui->btn_lut_off, lv_obj_get_child(ui->btn_lut_off, 0),
                              (!ui->lut_sd_mode && cur_sel == 0u), NULL);
        }
        for (uint32_t c = 0u; c < ZVP_LUT_CAT_COUNT; c++) {
            if (ui->lut_cat_btns[c]) {
                set_badge_checked(ui->lut_cat_btns[c], lv_obj_get_child(ui->lut_cat_btns[c], 0),
                                  (!ui->lut_sd_mode && (uint8_t)c == ui->lut_cat), NULL);
            }
        }
        if (ui->btn_lut_sd) {
            set_badge_checked(ui->btn_lut_sd, lv_obj_get_child(ui->btn_lut_sd, 0),
                              ui->lut_sd_mode, NULL);
        }

        if (ui->lut_sd_mode) {
            uint32_t sd_vis = lut_sd_visible_rows(ui);
            for (uint32_t r = 0u; r < ZVP_LUT_NUM_SLOTS; r++) {
                lv_obj_t *row = ui->lut_row_btns[r];
                if (!row) continue;
                if (r >= sd_vis) {
                    lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
                    continue;
                }
                lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
                lv_obj_t *lbl_title = lv_obj_get_child(row, 0);
                lv_obj_t *lbl_badge = ui->lut_row_badges[r];
                char row_buf[80];
                char badge_buf[16];
                bool is_cur = false;
                if (!ui->lut_sd_has_card_files && r == 0u) {
                    snprintf(row_buf, sizeof(row_buf), "无ZVC包");
                    snprintf(badge_buf, sizeof(badge_buf), "重扫");
                } else {
                    uint32_t item_idx = ui->lut_sd_has_card_files ? r : (r - 1u);
                    const zvp_lut_sd_item_t *it = &ui->lut_sd_items[item_idx];
                    snprintf(row_buf, sizeof(row_buf), "%s", it->name);
                    snprintf(badge_buf, sizeof(badge_buf), "%u", (unsigned)it->look_count);
                    if (it->is_embedded && rt->lut.bundle_from_embedded) is_cur = true;
                }
                if (lbl_title) {
                    const char *cur_txt = lv_label_get_text(lbl_title);
                    if (!cur_txt || strcmp(cur_txt, row_buf) != 0) lv_label_set_text(lbl_title, row_buf);
                    lv_label_long_mode_t wm = (ui->focused_obj == row) ? LV_LABEL_LONG_SCROLL : LV_LABEL_LONG_CLIP;
                    if (lv_label_get_long_mode(lbl_title) != wm) lv_label_set_long_mode(lbl_title, wm);
                }
                if (lbl_badge) lv_label_set_text(lbl_badge, badge_buf);
                set_badge_checked(row, lbl_title, is_cur, NULL);
                if (lbl_badge) lv_obj_set_style_text_color(lbl_badge, is_cur ? ZVP_COLOR_BG_BLACK : ZVP_COLOR_TEXT_SUB, 0);
            }
        } else {
            lut_rebuild_visible_rows(ui, &rt->lut);
        for (uint32_t r = 0u; r < ZVP_LUT_NUM_SLOTS; r++) {
            lv_obj_t *row = ui->lut_row_btns[r];
            if (!row) continue;
            if (r >= ui->lut_visible_count) {
                lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);

            uint32_t slot = ui->lut_row_slots[r];
            lv_obj_t *lbl_title = lv_obj_get_child(row, 0);
            lv_obj_t *lbl_badge = ui->lut_row_badges[r];
            char row_buf[80];
            const char *name = (slot < nslots) ? rt->lut.look_names[slot] : "LUT";
            snprintf(row_buf, sizeof(row_buf), "%02u %s", (unsigned)slot, name);
            if (lbl_title) {
                const char *cur_txt = lv_label_get_text(lbl_title);
                if (!cur_txt || strcmp(cur_txt, row_buf) != 0) {
                    lv_label_set_text(lbl_title, row_buf);
                }
            }

            bool is_active = (slot > 0u && slot == cur_sel);
            if (lbl_title) {
                lv_label_long_mode_t want_mode = is_active ? LV_LABEL_LONG_SCROLL : LV_LABEL_LONG_CLIP;
                if (lv_label_get_long_mode(lbl_title) != want_mode) {
                    lv_label_set_long_mode(lbl_title, want_mode);
                }
            }
            bool is_mod = zvp_lut_slot_is_modified(&rt->lut, slot);
            if (lbl_badge) {
                lv_label_set_text(lbl_badge, is_mod ? "*" : "");
            }

            if (is_active) {
                lv_obj_add_state(row, LV_STATE_CHECKED);
                if (lbl_title) lv_obj_set_style_text_color(lbl_title, ZVP_COLOR_BG_BLACK, 0);
                if (lbl_badge) lv_obj_set_style_text_color(lbl_badge, ZVP_COLOR_BG_BLACK, 0);
            } else {
                lv_obj_clear_state(row, LV_STATE_CHECKED);
                if (lbl_title) lv_obj_set_style_text_color(lbl_title, ZVP_COLOR_WHITE, 0);
                if (lbl_badge) lv_obj_set_style_text_color(lbl_badge, ZVP_COLOR_TEXT_SUB, 0);
            }
        }
        }
    }

    if (ui->lut_fine_layer && ui->lut_fine_visible && rt->lut.active_sel > 0u && rt->lut.active_sel < rt->lut.num_slots) {
        uint32_t fsel = rt->lut.active_sel;
        const zvp_lut_adjust_t *adj = &rt->lut.adjust[fsel];
        int8_t sh = rt->lut.sharpness[fsel];
        char vbufs[ZVP_LUT_PARAM_COUNT][16];
        snprintf(vbufs[ZVP_LUT_PARAM_STRENGTH],   16, "%u",  (unsigned)adj->strength10 * 10u);
        snprintf(vbufs[ZVP_LUT_PARAM_CONTRAST],   16, "%+d", (int)adj->contrast);
        snprintf(vbufs[ZVP_LUT_PARAM_HIGHLIGHTS], 16, "%+d", (int)adj->highlights);
        snprintf(vbufs[ZVP_LUT_PARAM_SHADOWS],    16, "%+d", (int)adj->shadows);
        snprintf(vbufs[ZVP_LUT_PARAM_FADE],       16, "%u",  (unsigned)adj->fade);
        snprintf(vbufs[ZVP_LUT_PARAM_SATURATION], 16, "%+d", (int)adj->saturation);
        if (sh == ZVP_LUT_SHARPNESS_NATIVE) {
            snprintf(vbufs[ZVP_LUT_PARAM_SHARPNESS], 16, "原生");
        } else {
            snprintf(vbufs[ZVP_LUT_PARAM_SHARPNESS], 16, "%+d", (int)sh);
        }
        for (uint32_t p = 0u; p < ZVP_LUT_PARAM_COUNT; p++) {
            if (ui->lut_param_vals[p]) {
                lv_label_set_text(ui->lut_param_vals[p], vbufs[p]);
            }
        }
    }

    /* 5GHz 状态更新 */
    set_badge_checked(ui->badge_wifi5g, ui->lbl_wifi5g_badge,
                      rt->wifi.enabled, rt->wifi.enabled ? "已开启" : "已关闭");
    lv_obj_set_style_opa(ui->ch_grid, rt->wifi.enabled ? LV_OPA_COVER : 175, 0);
    lv_label_set_text(ui->lbl_wifi_status, rt->wifi.last_msg);

    size_t cnt = 0;
    const uint8_t *chs = zvp_wifi5g_get_channels(&cnt);
    for (size_t i = 0; i < cnt && i < ZVP_WIFI5G_CHANNEL_COUNT; ++i) {
        lv_obj_t *btn = ui->ch_btns[i];
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        if (rt->wifi.enabled && rt->wifi.selected_channel == chs[i]) {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
            if (lbl) lv_obj_set_style_text_color(lbl, ZVP_COLOR_BG_BLACK, 0);
        } else {
            lv_obj_clear_state(btn, LV_STATE_CHECKED);
            if (lbl) lv_obj_set_style_text_color(lbl, ZVP_COLOR_WHITE, 0);
        }
    }

    /* 自定义快门音页及小悬浮窗更新 */
    set_badge_checked(ui->badge_shutter_enable, ui->lbl_shutter_badge,
                      rt->shutter_sound_enabled, rt->shutter_sound_enabled ? "已开启" : "已关闭");
    lv_label_set_text(ui->lbl_shutter_status,
                      rt->shutter_sound_enabled ? "快门按下自动播放" : "已关闭 (点击或OK开启)");
    set_badge_checked(ui->badge_mini_enable, ui->lbl_mini_badge,
                      rt->mini_widget_enabled, rt->mini_widget_enabled ? "已开启" : "已关闭");

    const char *cur_snd = rt->sound.selected_name[0] ? rt->sound.selected_name : "内置: 清脆快门";
    lv_label_set_text(ui->lbl_sound_name, cur_snd);
    if (rt->sound.last_source_label[0]) {
        lv_label_set_text(ui->lbl_sound_src, rt->sound.last_source_label);
    }

    char test_buf[96];
    if (rt->sound.last_error != 0) {
        snprintf(test_buf, sizeof(test_buf), "转换失败 err=%d (请检查16bit PCM WAV)", (int)rt->sound.last_error);
    } else if (rt->sound.play_count > 0u) {
        snprintf(test_buf, sizeof(test_buf), "已播放 %u 次 (%uB F0)",
                 (unsigned)rt->sound.play_count, (unsigned)rt->sound.last_f0_bytes);
    } else {
        snprintf(test_buf, sizeof(test_buf), "目录: PRIVATE/M4ROOT/GENERAL/SOUNDS");
    }
    lv_label_set_text(ui->lbl_sound_test, test_buf);

    if (ui->lbl_mini_play) {
        char mini_txt[48];
        const char *short_name = cur_snd;
        if (strncmp(cur_snd, "内置: ", sizeof("内置: ") - 1u) == 0) {
            short_name = cur_snd + (sizeof("内置: ") - 1u);
        }
        snprintf(mini_txt, sizeof(mini_txt), "播: %.14s", short_name);
        lv_label_set_text(ui->lbl_mini_play, mini_txt);
    }

    /* 纵横比页面更新 */
    if (ui->btn_aspect_enable) {
        const zvp_aspect_preset_t *cur_p = zvp_aspect_get_current_preset(&rt->aspect);
        set_badge_checked(ui->badge_aspect_enable, ui->lbl_aspect_badge,
                          rt->aspect.enabled, rt->aspect.enabled ? "已开启" : "已关闭");
        char asp_buf[96];
        if (rt->aspect.enabled && rt->aspect.layout.valid) {
            snprintf(asp_buf, sizeof(asp_buf), "%s · %dx%d (画面 %dx%d)",
                     cur_p->label,
                     (int)rt->aspect.layout.frame_rect.w,
                     (int)rt->aspect.layout.frame_rect.h,
                     (int)rt->aspect.layout.view_rect.w,
                     (int)rt->aspect.layout.view_rect.h);
        } else {
            snprintf(asp_buf, sizeof(asp_buf), "已选中 %s · 去底栏实拍基准 %dx%d",
                     cur_p->label,
                     (int)rt->aspect.layout.view_rect.w,
                     (int)rt->aspect.layout.view_rect.h);
        }
        lv_label_set_text(ui->lbl_aspect_status, asp_buf);
        lv_obj_set_style_opa(ui->aspect_grid, rt->aspect.enabled ? LV_OPA_COVER : 185, 0);

        for (size_t i = 0; i < ZVP_ASPECT_PRESET_COUNT; ++i) {
            lv_obj_t *btn = ui->aspect_btns[i];
            lv_obj_t *lbl = btn ? lv_obj_get_child(btn, 0) : NULL;
            if (!btn) continue;
            if (rt->aspect.preset_idx == (uint8_t)i) {
                lv_obj_add_state(btn, LV_STATE_CHECKED);
                if (lbl) lv_obj_set_style_text_color(lbl, ZVP_COLOR_BG_BLACK, 0);
            } else {
                lv_obj_clear_state(btn, LV_STATE_CHECKED);
                if (lbl) lv_obj_set_style_text_color(lbl, ZVP_COLOR_WHITE, 0);
            }
        }
        lv_label_set_text(ui->lbl_aspect_style, zvp_aspect_style_name(rt->aspect.style));
        lv_label_set_text(ui->lbl_aspect_guide, zvp_aspect_guide_name(rt->aspect.guide));
        lv_label_set_text(ui->lbl_aspect_par, zvp_aspect_par_mode_name(rt->aspect.par_mode));
    }

    /* 设置与安全页更新 */
    bool c1_excl = (rt->input.c1_mode == ZVP_C1_MODE_SHORT_EXCLUSIVE);
    set_badge_checked(ui->badge_c1_mode, ui->lbl_c1_mode,
                      c1_excl, c1_excl ? "短按独占" : "长按推荐");

    set_badge_checked(ui->badge_auto_flip, ui->lbl_auto_flip_badge,
                      rt->input.auto_flip, rt->input.auto_flip ? "已开启" : "已关闭");
    lv_label_set_text(ui->lbl_flip_state,
                      rt->input.auto_flip
                          ? (rt->input.hw_flipped ? "自动跟随 · 180°" : "自动跟随 · 正向")
                          : "保持正向");

    if (ui->btn_native_touch) {
        zvp_menu_touch_stats_t mtf_st;
        zvp_menu_touch_get_stats(&mtf_st);
        bool mtf_on = rt->native_menu_touch_enabled;
        set_badge_checked(ui->badge_native_touch, ui->lbl_native_touch_badge,
                          mtf_on, mtf_on ? "已开启" : "已关闭");
        if (!mtf_on) {
            lv_label_set_text(ui->lbl_native_touch_state, "已暂停 · 保持原厂无触控菜单");
        } else if (mtf_st.hooks_installed) {
            lv_label_set_text_fmt(ui->lbl_native_touch_state,
                                  "已就绪 · 已处理 %u 次点击",
                                  (unsigned)mtf_st.events_handled);
        } else {
            lv_label_set_text(ui->lbl_native_touch_state, "钩子待激活 · 检查基址/序言");
        }
    }

    if (rt->rec.phase == ZVP_REC_ARMED) {
        lv_obj_add_state(ui->btn_recovery, LV_STATE_CHECKED);
        lv_obj_t *lbl = lv_obj_get_child(ui->btn_recovery, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, ZVP_COLOR_BG_BLACK, 0);
    } else {
        lv_obj_clear_state(ui->btn_recovery, LV_STATE_CHECKED);
        lv_obj_t *lbl = lv_obj_get_child(ui->btn_recovery, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, ZVP_COLOR_WHITE, 0);
    }
    lv_label_set_text(ui->lbl_recovery, rt->rec.status_text);

    /* 2K cRAW 纯内存录制页更新 */
    if (ui->btn_craw_mode) {
        const char *badge_txt = "旁路";
        bool active = (rt->craw.mode != ZVP_CRAW_LIRO_BYPASS);
        if (rt->craw.mode == ZVP_CRAW_LIRO_SPY_ONLY) {
            badge_txt = "观测";
        } else if (rt->craw.mode == ZVP_CRAW_LIRO_ARM_2K) {
            badge_txt = "2K接管";
        } else if (rt->craw.mode == ZVP_CRAW_LIRO_RAW_W0) {
            badge_txt = "W0RAW";
        }
        set_badge_checked(ui->badge_craw_mode, ui->lbl_craw_mode_badge, active, badge_txt);
        lv_label_set_text(ui->lbl_craw_mode_desc,
                          rt->craw.status_msg[0] ? rt->craw.status_msg : "未挂载");

        if (ui->lbl_craw_pool) {
            char pbuf[48];
            if (rt->craw.pool_allocated) {
                snprintf(pbuf, sizeof(pbuf), "%.1fM/%u槽",
                         (double)rt->craw.pool_total_bytes / 1048576.0,
                         (unsigned)rt->craw.total_slots);
            } else {
                snprintf(pbuf, sizeof(pbuf), "未分配");
            }
            lv_label_set_text(ui->lbl_craw_pool, pbuf);
        }
        if (ui->lbl_craw_snap) {
            char sbuf[48];
            snprintf(sbuf, sizeof(sbuf), "已存 %u 帧", (unsigned)rt->craw.frames_written);
            lv_label_set_text(ui->lbl_craw_snap, sbuf);
        }
        if (ui->lbl_craw_telem_1) {
            char t1[128];
            snprintf(t1, sizeof(t1),
                     "Rdc:%u (%ux%u e%u) PA:%08X | Aif:%u p%u",
                     (unsigned)rt->craw.shm_snap.rdc_calls,
                     (unsigned)rt->craw.shm_snap.rdc_last_w,
                     (unsigned)rt->craw.shm_snap.rdc_last_h,
                     (unsigned)rt->craw.shm_snap.rdc_last_enc,
                     (unsigned)rt->craw.shm_snap.last_ring_pa_written,
                     (unsigned)rt->craw.shm_snap.aif_calls,
                     (unsigned)rt->craw.shm_snap.aif_last_port);
            lv_label_set_text(ui->lbl_craw_telem_1, t1);
        }
        if (ui->lbl_craw_telem_2) {
            char t2[128];
            snprintf(t2, sizeof(t2),
                     "ST:%u REC:%u DF:%u Mask:0x%x SD:%uMB",
                     (unsigned)rt->craw.shm_snap.state_calls,
                     (unsigned)rt->craw.shm_snap.rec_state,
                     (unsigned)rt->craw.shm_snap.dfe_rdcw0_calls,
                     (unsigned)rt->craw.shm_snap.hook_installed_mask,
                     (unsigned)rt->craw.free_sd_mb);
            lv_label_set_text(ui->lbl_craw_telem_2, t2);
        }
    }

    /* 屏幕校色页实时刷新 */
    if (ui->btn_cal_enable) {
        bool cal_on = rt->color_cal.enabled;
        bool cal_cmp = cal_on && rt->color_cal.compare_original;
        set_badge_checked(ui->badge_cal_enable, ui->lbl_cal_badge,
                          cal_on, cal_on ? "已开启" : "已关闭");
        if (ui->btn_cal_compare && ui->lbl_cal_compare) {
            if (cal_cmp) {
                lv_obj_add_state(ui->btn_cal_compare, LV_STATE_CHECKED);
                lv_obj_t *t = lv_obj_get_child(ui->btn_cal_compare, 0);
                if (t) lv_obj_set_style_text_color(t, ZVP_COLOR_BG_BLACK, 0);
                lv_obj_set_style_text_color(ui->lbl_cal_compare, ZVP_COLOR_BG_BLACK, 0);
                lv_label_set_text(ui->lbl_cal_compare, "原色预览中");
            } else {
                lv_obj_clear_state(ui->btn_cal_compare, LV_STATE_CHECKED);
                lv_obj_t *t = lv_obj_get_child(ui->btn_cal_compare, 0);
                if (t) lv_obj_set_style_text_color(t, ZVP_COLOR_WHITE, 0);
                lv_obj_set_style_text_color(ui->lbl_cal_compare, ZVP_COLOR_TEXT_SUB, 0);
                lv_label_set_text(ui->lbl_cal_compare, cal_on ? "校色生效中" : "点击对比");
            }
        }
        if (ui->lbl_cal_preset) {
            const zvp_cal_preset_info_t *pi = zvp_cal_get_preset(rt->color_cal.preset_idx);
            lv_label_set_text(ui->lbl_cal_preset, pi ? pi->short_name : "自定义");
        }
        if (ui->lbl_cal_status) {
            char stbuf[96];
            snprintf(stbuf, sizeof(stbuf), "%s\n重刷:%u",
                     rt->color_cal.factory_captured ? "已备份原厂" : "待采集基准",
                     (unsigned)rt->color_cal.reapply_count);
            lv_label_set_text(ui->lbl_cal_status, stbuf);
        }

        static const char *const k_cal_top_names[ZVP_CAL_PARAM_COUNT] = {
            "色温(暖/冷)", "色调(红/绿)", "饱和度", "对比度", "伽马(明暗)", "红增益(R)", "绿增益(G)", "蓝增益(B)"
        };
        uint32_t sel_p = ((uint32_t)ui->cal_sel_param < ZVP_CAL_PARAM_COUNT)
                             ? (uint32_t)ui->cal_sel_param
                             : 0u;
        if (ui->lbl_cal_top_param) {
            if (cal_cmp) {
                lv_label_set_text(ui->lbl_cal_top_param, "原色对比预览中 · OK/点击恢复");
            } else {
                int v = (int)rt->color_cal.params.vals[sel_p];
                char tbuf[64];
                snprintf(tbuf, sizeof(tbuf), "%s: %s%d", k_cal_top_names[sel_p], (v > 0) ? "+" : "", v);
                lv_label_set_text(ui->lbl_cal_top_param, tbuf);
            }
        }
        for (uint32_t i = 0u; i < ZVP_CAL_PARAM_COUNT; i++) {
            if (ui->cal_param_btns[i] && ui->cal_param_vals[i]) {
                int v = (int)rt->color_cal.params.vals[i];
                char vbuf[16];
                snprintf(vbuf, sizeof(vbuf), "%s%d", (v > 0) ? "+" : "", v);
                bool active_tile = (i == sel_p);
                set_badge_checked(ui->cal_param_btns[i], lv_obj_get_child(ui->cal_param_btns[i], 0),
                                  active_tile, NULL);
                lv_obj_set_style_text_color(ui->cal_param_vals[i],
                                            active_tile ? ZVP_COLOR_BG_BLACK : ZVP_COLOR_WHITE, 0);
                lv_label_set_text(ui->cal_param_vals[i], vbuf);
            }
        }
    }

    if ((rt->input.menu_visible || rt->input.mini_widget_visible ||
         zvp_aspect_should_display(&rt->aspect, rt->input.menu_visible)) &&
        ui->scr) {
        lv_obj_invalidate(ui->scr);
    }
}

static void apply_overlay_bar(lv_obj_t *bar, const zvp_aspect_rect_t *r, uint8_t opa)
{
    if (!bar) return;
    if (!r || r->w <= 0 || r->h <= 0 || opa == 0u) {
        lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_set_pos(bar, r->x, r->y);
    lv_obj_set_size(bar, r->w, r->h);
    lv_obj_set_style_bg_opa(bar, opa, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);
}

static void apply_overlay_line(lv_obj_t *line, int16_t x, int16_t y, int16_t w, int16_t h, bool show)
{
    if (!line) return;
    if (!show || w <= 0 || h <= 0) {
        lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_set_pos(line, x, y);
    lv_obj_set_size(line, w, h);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_HIDDEN);
}

static void sync_aspect_overlay_visual(zvp_ui_t *ui, const zvp_runtime_t *rt, bool show_aspect)
{
    if (!ui || !ui->built || !ui->aspect_layer || !rt) {
        return;
    }

    static bool s_prev_show = false;
    static zvp_aspect_layout_t s_prev_layout;
    static zvp_aspect_style_t s_prev_style = ZVP_ASPECT_STYLE_COUNT;
    static zvp_aspect_guide_t s_prev_guide = ZVP_ASPECT_GUIDE_COUNT;

    if (!show_aspect || !rt->aspect.layout.valid) {
        if (s_prev_show) {
            s_prev_show = false;
            lv_obj_add_flag(ui->aspect_layer, LV_OBJ_FLAG_HIDDEN);
            if (ui->scr) {
                lv_obj_invalidate(ui->scr);
            }
        }
        return;
    }

    const zvp_aspect_layout_t *ly = &rt->aspect.layout;
    bool unchanged = s_prev_show &&
                     s_prev_style == rt->aspect.style &&
                     s_prev_guide == rt->aspect.guide &&
                     memcmp(&s_prev_layout, ly, sizeof(*ly)) == 0;
    if (unchanged) {
        return;
    }

    s_prev_show = true;
    s_prev_style = rt->aspect.style;
    s_prev_guide = rt->aspect.guide;
    s_prev_layout = *ly;

    uint8_t mask_opa = zvp_aspect_style_opa(rt->aspect.style);
    apply_overlay_bar(ui->aspect_bars[0], &ly->bar_top, mask_opa);
    apply_overlay_bar(ui->aspect_bars[1], &ly->bar_bottom, mask_opa);
    apply_overlay_bar(ui->aspect_bars[2], &ly->bar_left, mask_opa);
    apply_overlay_bar(ui->aspect_bars[3], &ly->bar_right, mask_opa);

    if (rt->aspect.guide != ZVP_ASPECT_GUIDE_NONE &&
        ly->frame_rect.w > 2 && ly->frame_rect.h > 2) {
        int32_t bw = (rt->aspect.style == ZVP_ASPECT_STYLE_FRAME_ONLY) ? 2 : 1;
        uint8_t b_opa = (rt->aspect.style == ZVP_ASPECT_STYLE_FRAME_ONLY) ? 230u : 175u;
        lv_obj_set_pos(ui->aspect_frame_box, ly->frame_rect.x, ly->frame_rect.y);
        lv_obj_set_size(ui->aspect_frame_box, ly->frame_rect.w, ly->frame_rect.h);
        lv_obj_set_style_border_width(ui->aspect_frame_box, bw, 0);
        lv_obj_set_style_border_opa(ui->aspect_frame_box, b_opa, 0);
        lv_obj_clear_flag(ui->aspect_frame_box, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui->aspect_frame_box, LV_OBJ_FLAG_HIDDEN);
    }

    bool show_thirds = (rt->aspect.guide == ZVP_ASPECT_GUIDE_THIRDS) &&
                       (ly->frame_rect.w >= 30) && (ly->frame_rect.h >= 30);
    int16_t fx = ly->frame_rect.x;
    int16_t fy = ly->frame_rect.y;
    int16_t fw = ly->frame_rect.w;
    int16_t fh = ly->frame_rect.h;
    apply_overlay_line(ui->aspect_thirds[0], (int16_t)(fx + fw / 3), fy, 1, fh, show_thirds);
    apply_overlay_line(ui->aspect_thirds[1], (int16_t)(fx + (fw * 2) / 3), fy, 1, fh, show_thirds);
    apply_overlay_line(ui->aspect_thirds[2], fx, (int16_t)(fy + fh / 3), fw, 1, show_thirds);
    apply_overlay_line(ui->aspect_thirds[3], fx, (int16_t)(fy + (fh * 2) / 3), fw, 1, show_thirds);

    lv_obj_clear_flag(ui->aspect_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(ui->aspect_layer);
    if (ui->scr) {
        lv_obj_invalidate(ui->scr);
    }
}

void zvp_ui_sync_overlay(zvp_ui_t *ui)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!ui || !rt) {
        return;
    }

    bool show_menu = rt->input.menu_visible;
    bool show_mini = (!show_menu) && rt->mini_widget_enabled;
    bool show_aspect = zvp_aspect_should_display(&rt->aspect, show_menu);
    bool need_overlay = show_menu || show_mini || show_aspect;

    static bool s_was_overlay = false;

    if (need_overlay) {
        if (zvp_bridge_ensure_canvas(&rt->bridge) == 0 && !rt->lvgl.initialized) {
            if (zvp_lvgl_port_init(&rt->lvgl, &rt->bridge) == 0) {
                zvp_ui_build(ui);
            }
        }
        if (ui->built) {
            if (show_menu) {
                /* 选中非 0 LUT 且非 SD 包列表态时，右下独立微调条随二级列表同屏显示 */
                bool show_fine = (ui->active_page == ZVP_PAGE_LUT) &&
                                 (!ui->lut_sd_mode) &&
                                 (rt->lut.active_sel > 0u) &&
                                 (rt->lut.active_sel < rt->lut.num_slots);
                ui->lut_fine_visible = show_fine;
                lv_obj_clear_flag(ui->left_nav, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(ui->right_panel, LV_OBJ_FLAG_HIDDEN);
                if (ui->lut_top_layer) {
                    if (ui->active_page == ZVP_PAGE_LUT) {
                        lv_obj_clear_flag(ui->lut_top_layer, LV_OBJ_FLAG_HIDDEN);
                    } else {
                        lv_obj_add_flag(ui->lut_top_layer, LV_OBJ_FLAG_HIDDEN);
                    }
                }
                if (ui->lut_fine_layer) {
                    if (show_fine) {
                        lv_obj_clear_flag(ui->lut_fine_layer, LV_OBJ_FLAG_HIDDEN);
                    } else {
                        lv_obj_add_flag(ui->lut_fine_layer, LV_OBJ_FLAG_HIDDEN);
                    }
                }
                bool show_cal_layers = (ui->active_page == ZVP_PAGE_COLOR_CAL);
                if (ui->cal_top_layer) {
                    if (show_cal_layers) lv_obj_clear_flag(ui->cal_top_layer, LV_OBJ_FLAG_HIDDEN);
                    else lv_obj_add_flag(ui->cal_top_layer, LV_OBJ_FLAG_HIDDEN);
                }
                if (ui->cal_fine_layer) {
                    if (show_cal_layers) lv_obj_clear_flag(ui->cal_fine_layer, LV_OBJ_FLAG_HIDDEN);
                    else lv_obj_add_flag(ui->cal_fine_layer, LV_OBJ_FLAG_HIDDEN);
                }
                lv_obj_add_flag(ui->mini_win, LV_OBJ_FLAG_HIDDEN);
            } else {
                ui->lut_fine_visible = false;
                lv_obj_add_flag(ui->left_nav, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui->right_panel, LV_OBJ_FLAG_HIDDEN);
                if (ui->lut_top_layer) {
                    lv_obj_add_flag(ui->lut_top_layer, LV_OBJ_FLAG_HIDDEN);
                }
                if (ui->lut_fine_layer) {
                    lv_obj_add_flag(ui->lut_fine_layer, LV_OBJ_FLAG_HIDDEN);
                }
                if (ui->cal_top_layer) {
                    lv_obj_add_flag(ui->cal_top_layer, LV_OBJ_FLAG_HIDDEN);
                }
                if (ui->cal_fine_layer) {
                    lv_obj_add_flag(ui->cal_fine_layer, LV_OBJ_FLAG_HIDDEN);
                }
                if (show_mini) {
                    lv_obj_clear_flag(ui->mini_win, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(ui->mini_win, LV_OBJ_FLAG_HIDDEN);
                }
            }
            sync_aspect_overlay_visual(ui, rt, show_aspect);
            zvp_ui_refresh(ui);
            lv_obj_invalidate(ui->scr);
        }
        zvp_input_set_mini_widget(&rt->input,
                                  show_mini,
                                  ZVP_MINI_WIDGET_X,
                                  ZVP_MINI_WIDGET_Y,
                                  ZVP_MINI_WIDGET_W,
                                  ZVP_MINI_WIDGET_H);
        zvp_bridge_set_overlay_visible(&rt->bridge, true);
        s_was_overlay = true;
    } else {
        zvp_input_set_mini_widget(&rt->input,
                                  false,
                                  ZVP_MINI_WIDGET_X,
                                  ZVP_MINI_WIDGET_Y,
                                  ZVP_MINI_WIDGET_W,
                                  ZVP_MINI_WIDGET_H);
        if (ui->built) {
            lv_obj_add_flag(ui->left_nav, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui->right_panel, LV_OBJ_FLAG_HIDDEN);
            if (ui->lut_top_layer) {
                lv_obj_add_flag(ui->lut_top_layer, LV_OBJ_FLAG_HIDDEN);
            }
            if (ui->lut_fine_layer) {
                lv_obj_add_flag(ui->lut_fine_layer, LV_OBJ_FLAG_HIDDEN);
            }
            if (ui->cal_top_layer) {
                lv_obj_add_flag(ui->cal_top_layer, LV_OBJ_FLAG_HIDDEN);
            }
            if (ui->cal_fine_layer) {
                lv_obj_add_flag(ui->cal_fine_layer, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_add_flag(ui->mini_win, LV_OBJ_FLAG_HIDDEN);
            sync_aspect_overlay_visual(ui, rt, false);
        }
        if (s_was_overlay) {
            zvp_bridge_clear_canvas(&rt->bridge);
            s_was_overlay = false;
        }
        zvp_bridge_set_overlay_visible(&rt->bridge, false);
    }
}

void zvp_ui_set_visible(zvp_ui_t *ui, bool visible)
{
    (void)visible;
    zvp_ui_sync_overlay(ui);
}

void zvp_ui_set_lut_fine_mode(zvp_ui_t *ui, bool enable)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!ui || !ui->built || !rt) {
        return;
    }
    if (enable && rt->lut.active_sel > 0u) {
        if ((uint32_t)ui->lut_fine_param >= ZVP_LUT_PARAM_COUNT) {
            ui->lut_fine_param = ZVP_LUT_PARAM_STRENGTH;
        }
        set_focus_obj(ui, ui->lut_param_btns[ui->lut_fine_param]);
    } else {
        lut_focus_slot_or_cat(ui, &rt->lut);
    }
    zvp_ui_sync_overlay(ui);
}
