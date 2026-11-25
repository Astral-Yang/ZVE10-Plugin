#include "zvp_runtime.h"

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    zvp_runtime_t *rt = zvp_runtime_get();
    if (rt && rt->bridge.canvas_ready) {
        int32_t w = area->x2 - area->x1 + 1;
        bool rot180 = zvp_input_is_ui_rotated_180(&rt->input);
        zvp_bridge_blit_argb8888(&rt->bridge,
                                 area->x1, area->y1,
                                 area->x2, area->y2,
                                 (const uint32_t *)px_map,
                                 w,
                                 rot180);
        if (lv_display_flush_is_last(disp)) {
            zvp_bridge_commit_sprite(&rt->bridge);
        }
    }
    lv_display_flush_ready(disp);
}

static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt || (!rt->input.menu_visible && !rt->input.mini_widget_visible)) {
        data->point.x = 0;
        data->point.y = 0;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    data->point.x = rt->lvgl.cur_touch_x;
    data->point.y = rt->lvgl.cur_touch_y;
    data->state = rt->lvgl.cur_touch_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void lvgl_keypad_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    zvp_runtime_t *rt = zvp_runtime_get();
    if (!rt) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (rt->lvgl.pending_key_pressed) {
        data->key = rt->lvgl.pending_key;
        data->state = LV_INDEV_STATE_PRESSED;
        /* 下一次 read_cb 自动上报 RELEASED 完成点击周期 */
        rt->lvgl.pending_key_pressed = false;
    } else {
        data->key = rt->lvgl.pending_key;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

int zvp_lvgl_port_init(zvp_lvgl_port_t *port, zvp_native_bridge_t *br)
{
    (void)br;
    if (!port) {
        return -1;
    }
    if (port->initialized) {
        return 0;
    }

    lv_init();

    port->disp = lv_display_create(ZVP_SCREEN_WIDTH, ZVP_SCREEN_HEIGHT);
    if (!port->disp) {
        return -1;
    }

    lv_display_set_color_format(port->disp, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_flush_cb(port->disp, lvgl_flush_cb);
    lv_display_set_buffers(port->disp,
                           port->draw_buf,
                           NULL,
                           sizeof(port->draw_buf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    port->indev_touch = lv_indev_create();
    lv_indev_set_type(port->indev_touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(port->indev_touch, lvgl_touch_read_cb);
    lv_indev_set_mode(port->indev_touch, LV_INDEV_MODE_EVENT);
    lv_indev_set_scroll_limit(port->indev_touch, 16u);

    port->indev_keypad = lv_indev_create();
    lv_indev_set_type(port->indev_keypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(port->indev_keypad, lvgl_keypad_read_cb);

    port->last_tick_ms = (uint32_t)zvp_monotonic_ms();
    port->initialized = true;
    zvp_logf("LVGL v9.2 端口初始化完成 (640x480 ARGB8888->RGBA4444)");
    return 0;
}

void zvp_lvgl_port_push_key(zvp_lvgl_port_t *port, uint32_t lv_key)
{
    if (!port) {
        return;
    }
    port->pending_key = lv_key;
    port->pending_key_pressed = true;
}

void zvp_lvgl_port_tick_and_render(zvp_lvgl_port_t *port)
{
    if (!port || !port->initialized) {
        return;
    }
    uint32_t now = (uint32_t)zvp_monotonic_ms();
    uint32_t delta = now - port->last_tick_ms;
    if (delta > 0u) {
        if (delta > 200u) {
            delta = 33u;
        }
        lv_tick_inc(delta);
        port->last_tick_ms = now;
    }
    lv_timer_handler();
}
