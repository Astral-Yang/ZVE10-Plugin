#include "zvp_runtime.h"
#include <stdio.h>
#include <string.h>

/* 在 src/zvp_font_zh24.c 与 src/zvp_font_zh18.c 中导出的全字符集大号抗锯齿中文字体 */
extern const lv_font_t zvp_font_zh24;
extern const lv_font_t zvp_font_zh18;

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
    lv_obj_set_style_pad_row(page, 12, 0);

    /* 1. C1 唤醒按键切换整行可点大卡片 */
    ui->btn_c1_mode = create_click_card(page, 430, 96);
    lv_obj_set_flex_flow(ui->btn_c1_mode, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->btn_c1_mode, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(ui->btn_c1_mode, on_c1_mode_focus_clicked, LV_EVENT_CLICKED, NULL);
    make_label(ui->btn_c1_mode, "C1 按键唤醒", &zvp_font_zh24, ZVP_COLOR_WHITE);

    ui->badge_c1_mode = make_state_badge(ui->btn_c1_mode, 162, 54, "短按独占", &ui->lbl_c1_mode);
    lv_obj_add_flag(ui->badge_c1_mode, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui->badge_c1_mode, on_c1_badge_clicked, LV_EVENT_CLICKED, NULL);

    /* 2. 屏幕翻转自适应整行可点大卡片 */
    ui->btn_flip_card = create_click_card(page, 430, 96);
    lv_obj_set_flex_flow(ui->btn_flip_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->btn_flip_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(ui->btn_flip_card, on_flip_card_focus_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *flip_txt = lv_obj_create(ui->btn_flip_card);
    lv_obj_remove_style_all(flip_txt);
    lv_obj_set_size(flip_txt, 274, 70);
    lv_obj_set_flex_flow(flip_txt, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(flip_txt, 4, 0);
    lv_obj_clear_flag(flip_txt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(flip_txt, LV_OBJ_FLAG_SCROLLABLE);
    make_label(flip_txt, "屏幕翻转自适应", &zvp_font_zh24, ZVP_COLOR_WHITE);
    ui->lbl_flip_state = make_label(flip_txt, "自动跟随 · 正向", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);

    ui->badge_auto_flip = make_state_badge(ui->btn_flip_card, 114, 54, "已开启", &ui->lbl_auto_flip_badge);
    lv_obj_add_flag(ui->badge_auto_flip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui->badge_auto_flip, on_flip_badge_clicked, LV_EVENT_CLICKED, NULL);

    /* 3. 安全复位大卡片 */
    lv_obj_t *rec_card = create_card(page, 430, 152);
    lv_obj_set_flex_flow(rec_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(rec_card, 12, 0);

    ui->btn_recovery = lv_button_create(rec_card);
    lv_obj_set_size(ui->btn_recovery, 400, 74);
    style_mono_btn(ui->btn_recovery);
    lv_obj_add_event_cb(ui->btn_recovery, on_recovery_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rec_btn_lbl = make_label(ui->btn_recovery, "安全复位 (全量恢复默认)", &zvp_font_zh24, ZVP_COLOR_WHITE);
    lv_obj_center(rec_btn_lbl);

    ui->lbl_recovery = make_label(rec_card, "就绪", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);

    /* 4. 卡控熔断门禁简况 */
    ui->lbl_gate_info = make_label(page, "卡控: 已插卡激活 · A:/ZVP_OFF 可禁用", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);
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
    lv_obj_set_style_pad_all(ui->scr, 8, 0);
    lv_obj_set_scrollbar_mode(ui->scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui->scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui->scr, LV_OBJ_FLAG_CLICKABLE);

    /* 左侧小面积一级菜单栏 (162x464) */
    ui->left_nav = lv_obj_create(ui->scr);
    lv_obj_set_pos(ui->left_nav, 0, 0);
    lv_obj_set_size(ui->left_nav, 162, 464);
    style_glass_panel(ui->left_nav, 10);
    lv_obj_set_style_pad_all(ui->left_nav, 8, 0);
    lv_obj_set_flex_flow(ui->left_nav, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui->left_nav, 6, 0);
    lv_obj_set_scrollbar_mode(ui->left_nav, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui->left_nav, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui->left_nav, LV_OBJ_FLAG_CLICKABLE);

    /* 可垂直滚动的一级菜单列表容器 */
    ui->nav_list = lv_obj_create(ui->left_nav);
    lv_obj_remove_style_all(ui->nav_list);
    lv_obj_set_size(ui->nav_list, 146, 418);
    lv_obj_set_flex_flow(ui->nav_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui->nav_list, 6, 0);
    lv_obj_set_style_bg_opa(ui->nav_list, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(ui->nav_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui->nav_list, LV_DIR_VER);
    lv_obj_clear_flag(ui->nav_list, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scrollbar_mode(ui->nav_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(ui->nav_list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(ui->nav_list, 1, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(ui->nav_list, lv_color_hex(0xFFFFFF), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(ui->nav_list, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(ui->nav_list, 2, LV_PART_SCROLLBAR);

    static const char *nav_titles[ZVP_PAGE_COUNT] = {
        "创意外观",
        "LUT",
        "5GHz WIFI",
        "文件共享",
        "快门音效",
        "偏色校准",
        "纵横比",
        "USB外录",
        "1080P 超采",
        "设置与安全"
    };
    for (int i = 0; i < ZVP_PAGE_COUNT; ++i) {
        lv_obj_t *btn = lv_button_create(ui->nav_list);
        lv_obj_set_size(btn, 146, 62);
        style_mono_btn(btn);
        lv_obj_set_style_pad_hor(btn, 2, 0);
        lv_obj_set_style_pad_ver(btn, 6, 0);
        lv_obj_add_event_cb(btn, on_nav_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *lbl = make_label(btn, nav_titles[i], &zvp_font_zh24, ZVP_COLOR_WHITE);
        lv_obj_center(lbl);
        ui->nav_btns[i] = btn;
    }

    ui->lbl_nav_hint = make_label(ui->left_nav, "C1 键开启 / 关闭", &zvp_font_zh18, ZVP_COLOR_TEXT_SUB);
    lv_obj_set_style_pad_top(ui->lbl_nav_hint, 4, 0);

    /* 右侧大面积二级操作区 (450x464，留出 12px 隔离带防止左栏边缘误触) */
    ui->right_panel = lv_obj_create(ui->scr);
    lv_obj_set_pos(ui->right_panel, 174, 0);
    lv_obj_set_size(ui->right_panel, 450, 464);
    style_glass_panel(ui->right_panel, 10);
    lv_obj_set_style_pad_all(ui->right_panel, 10, 0);
    lv_obj_set_scrollbar_mode(ui->right_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui->right_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui->right_panel, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < ZVP_PAGE_COUNT; ++i) {
        lv_obj_t *p = lv_obj_create(ui->right_panel);
        lv_obj_remove_style_all(p);
        lv_obj_set_pos(p, 0, 0);
        lv_obj_set_size(p, 430, 442);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
        ui->pages[i] = p;
    }

    /* 创意外观 / LUT / 文件共享 / 偏色校准 / 纵横比 / USB外录 / 1080P 超采 暂无功能，二级菜单保持留空 */
    build_wifi5g_page(ui, ui->pages[ZVP_PAGE_WIFI5G]);
    build_custom_shutter_page(ui, ui->pages[ZVP_PAGE_CUSTOM_SHUTTER]);
    build_settings_page(ui, ui->pages[ZVP_PAGE_SETTINGS]);
    build_mini_widget(ui, ui->scr);

    ui->built = true;
    zvp_ui_switch_page(ui, ZVP_PAGE_CREATIVE_LOOK);
    set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_CREATIVE_LOOK]);
    zvp_ui_refresh(ui);
}

void zvp_ui_switch_page(zvp_ui_t *ui, zvp_ui_page_t page)
{
    if (!ui || !ui->built || page >= ZVP_PAGE_COUNT) {
        return;
    }
    ui->active_page = page;
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
}

/* 清晰直观的左右分栏 + 二维信道网格导航 */
bool zvp_ui_handle_nav_key(zvp_ui_t *ui, uint32_t act_mask)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!ui || !ui->built || !rt) {
        return false;
    }

    lv_obj_t *cur = ui->focused_obj;
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
                if (i == ZVP_PAGE_WIFI5G) {
                    first = ui->btn_wifi_card;
                } else if (i == ZVP_PAGE_CUSTOM_SHUTTER) {
                    first = ui->btn_shutter_enable;
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

    /* 4. 焦点位于右侧 Page (设置与安全) */
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
                set_focus_obj(ui, ui->btn_recovery);
            } else if (act_mask & ZVP_ACT_NAV_ENTER) {
                trigger_auto_flip_toggle(rt, "key");
            }
            return true;
        }
        if (cur == ui->btn_recovery) {
            if (act_mask & ZVP_ACT_NAV_LEFT) {
                set_focus_obj(ui, ui->nav_btns[ZVP_PAGE_SETTINGS]);
            } else if (act_mask & (ZVP_ACT_NAV_UP | ZVP_ACT_DIAL_PREV)) {
                set_focus_obj(ui, ui->btn_flip_card);
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

    return false;
}

void zvp_ui_refresh(zvp_ui_t *ui)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!ui || !ui->built || !rt) {
        return;
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

    if ((rt->input.menu_visible || rt->input.mini_widget_visible) && ui->scr) {
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
    bool need_overlay = show_menu || show_mini;

    if (need_overlay) {
        if (zvp_bridge_ensure_canvas(&rt->bridge) == 0 && !rt->lvgl.initialized) {
            if (zvp_lvgl_port_init(&rt->lvgl, &rt->bridge) == 0) {
                zvp_ui_build(ui);
            }
        }
        if (ui->built) {
            if (show_menu) {
                lv_obj_clear_flag(ui->left_nav, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(ui->right_panel, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui->mini_win, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(ui->left_nav, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui->right_panel, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(ui->mini_win, LV_OBJ_FLAG_HIDDEN);
            }
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
    } else {
        zvp_input_set_mini_widget(&rt->input,
                                  false,
                                  ZVP_MINI_WIDGET_X,
                                  ZVP_MINI_WIDGET_Y,
                                  ZVP_MINI_WIDGET_W,
                                  ZVP_MINI_WIDGET_H);
        if (ui->built) {
            lv_obj_add_flag(ui->mini_win, LV_OBJ_FLAG_HIDDEN);
        }
        zvp_bridge_set_overlay_visible(&rt->bridge, false);
    }
}

void zvp_ui_set_visible(zvp_ui_t *ui, bool visible)
{
    (void)visible;
    zvp_ui_sync_overlay(ui);
}
