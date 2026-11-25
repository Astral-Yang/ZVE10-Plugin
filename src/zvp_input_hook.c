#include "zvp_core.h"
#include "zvp_abi.h"
#include <string.h>

void zvp_input_init(zvp_input_state_t *st, uint32_t c1_keycode)
{
    if (!st) {
        return;
    }
    memset(st, 0, sizeof(*st));
    st->c1_mode = ZVP_C1_MODE_SHORT_EXCLUSIVE;
    st->menu_visible = false;
    st->auto_flip = true;
    st->hw_flipped = false;
    st->c1_keycode = (c1_keycode != 0) ? c1_keycode : ZVP_KEY_C1_DEFAULT;
}

void zvp_input_set_c1_mode(zvp_input_state_t *st, zvp_c1_mode_t mode)
{
    if (!st) {
        return;
    }
    st->c1_mode = mode;
    st->c1_is_down = false;
    st->c1_long_fired = false;
    st->c1_suppress_aux_up = false;
}

void zvp_input_set_auto_flip(zvp_input_state_t *st, bool enable)
{
    if (st) {
        st->auto_flip = enable;
    }
}

void zvp_input_update_flip(zvp_input_state_t *st, bool is_flipped)
{
    if (st) {
        st->hw_flipped = is_flipped;
    }
}

bool zvp_input_is_ui_rotated_180(const zvp_input_state_t *st)
{
    return st && st->auto_flip && st->hw_flipped;
}

uint32_t zvp_input_process_key(zvp_input_state_t *st,
                               uint32_t keycode,
                               bool is_press,
                               uint64_t now_ms)
{
    if (!st) {
        return ZVP_ACT_NONE;
    }

    if (keycode == ZVP_KEY_C1_DEFOCUS_AUX) {
        /* 短按独占模式、菜单开启状态或已触发长按时，一并吞掉伴生散焦/自定义事件 */
        if (st->c1_mode == ZVP_C1_MODE_SHORT_EXCLUSIVE || st->menu_visible ||
            st->c1_long_fired) {
            return ZVP_ACT_CONSUME_SYS;
        }
        /* 长按模式下等待 500ms 判别期间暂扣伴生按下事件，短按松手时一并回放给原厂 */
        if (is_press && st->c1_is_down) {
            return ZVP_ACT_DEFER_C1 | ZVP_ACT_CONSUME_SYS;
        }
        if (!is_press && st->c1_suppress_aux_up) {
            st->c1_suppress_aux_up = false;
            return ZVP_ACT_CONSUME_SYS;
        }
        return ZVP_ACT_NONE;
    }

    if (keycode == st->c1_keycode) {
        if (st->c1_mode == ZVP_C1_MODE_SHORT_EXCLUSIVE) {
            if (is_press) {
                if (!st->c1_is_down) {
                    st->c1_is_down = true;
                    st->menu_visible = !st->menu_visible;
                    return ZVP_ACT_TOGGLE_MENU | ZVP_ACT_CONSUME_SYS;
                }
                return ZVP_ACT_CONSUME_SYS;
            } else {
                st->c1_is_down = false;
                st->c1_long_fired = false;
                return ZVP_ACT_CONSUME_SYS;
            }
        } else {
            /* 默认长按模式 (>=500ms 触发，按下期间先暂扣，短按松手完整回放激活原厂功能) */
            if (is_press) {
                if (!st->c1_is_down) {
                    st->c1_is_down = true;
                    st->c1_long_fired = false;
                    st->c1_suppress_aux_up = false;
                    st->c1_press_ms = now_ms;
                    return st->menu_visible
                               ? ZVP_ACT_CONSUME_SYS
                               : (ZVP_ACT_DEFER_C1 | ZVP_ACT_CONSUME_SYS);
                }
                if (!st->c1_long_fired && (now_ms - st->c1_press_ms) >= ZVP_C1_LONG_PRESS_MS) {
                    st->c1_long_fired = true;
                    st->menu_visible = !st->menu_visible;
                    return ZVP_ACT_TOGGLE_MENU | ZVP_ACT_CONSUME_SYS;
                }
                return ZVP_ACT_CONSUME_SYS;
            } else {
                bool fired = st->c1_long_fired;
                uint64_t held = now_ms - st->c1_press_ms;
                bool was_down = st->c1_is_down;
                st->c1_is_down = false;
                st->c1_long_fired = false;

                if (fired) {
                    st->c1_suppress_aux_up = true;
                    return ZVP_ACT_CONSUME_SYS;
                }
                if (was_down && held >= ZVP_C1_LONG_PRESS_MS) {
                    st->c1_suppress_aux_up = true;
                    st->menu_visible = !st->menu_visible;
                    return ZVP_ACT_TOGGLE_MENU | ZVP_ACT_CONSUME_SYS;
                }
                if (st->menu_visible) {
                    st->c1_suppress_aux_up = true;
                    return ZVP_ACT_CONSUME_SYS;
                }
                /* 短按松手：触发回放已暂扣的按下包并放行松手事件给原厂 */
                return was_down ? ZVP_ACT_REPLAY_C1 : ZVP_ACT_NONE;
            }
        }
    }

    /* 菜单关闭状态下，所有方向键/确认键全部放行给相机 */
    if (!st->menu_visible) {
        return ZVP_ACT_NONE;
    }

    /* 菜单开启状态下，拦截导航按键并转为 UI 控制动作 */
    if (!is_press) {
        switch (keycode) {
            case ZVP_KEY_UP:
            case ZVP_KEY_DOWN:
            case ZVP_KEY_LEFT:
            case ZVP_KEY_RIGHT:
            case ZVP_KEY_ENTER:
            case ZVP_KEY_MENU:
            case ZVP_KEY_PLAY:
            case ZVP_KEY_DELETE:
            case ZVP_KEY_LEGACY_UP:
            case ZVP_KEY_LEGACY_DOWN:
            case ZVP_KEY_LEGACY_LEFT:
            case ZVP_KEY_LEGACY_RIGHT:
            case ZVP_KEY_LEGACY_ENTER:
            case ZVP_KEY_LEGACY_MENU:
                return ZVP_ACT_CONSUME_SYS;
            default:
                if (keycode == 0x15010020u || keycode == 0x15010030u ||
                    keycode == 0x15010230u || keycode == 0x15010290u) {
                    return ZVP_ACT_NONE;
                }
                if ((keycode & 0xFFFF0000u) == 0x15010000u ||
                    (keycode & 0xFFFF0000u) == 0x15020000u ||
                    (keycode & 0xFFFF0000u) == 0x15030000u) {
                    return ZVP_ACT_CONSUME_SYS;
                }
                return ZVP_ACT_NONE;
        }
    }

    switch (keycode) {
        case ZVP_KEY_UP:
        case ZVP_KEY_LEGACY_UP:
            return ZVP_ACT_NAV_UP    | ZVP_ACT_CONSUME_SYS;
        case ZVP_KEY_DOWN:
        case ZVP_KEY_LEGACY_DOWN:
            return ZVP_ACT_NAV_DOWN  | ZVP_ACT_CONSUME_SYS;
        case ZVP_KEY_LEFT:
        case ZVP_KEY_LEGACY_LEFT:
            return ZVP_ACT_NAV_LEFT  | ZVP_ACT_CONSUME_SYS;
        case ZVP_KEY_RIGHT:
        case ZVP_KEY_LEGACY_RIGHT:
            return ZVP_ACT_NAV_RIGHT | ZVP_ACT_CONSUME_SYS;
        case ZVP_KEY_ENTER:
        case ZVP_KEY_LEGACY_ENTER:
            return ZVP_ACT_NAV_ENTER | ZVP_ACT_CONSUME_SYS;
        case ZVP_KEY_MENU:
        case ZVP_KEY_LEGACY_MENU:
        case ZVP_KEY_PLAY:
        case ZVP_KEY_DELETE:
            /* 仅允许 C1 按键开启和关闭菜单，MENU 等按键在菜单打开期间仅做屏蔽拦截 */
            return ZVP_ACT_CONSUME_SYS;
        default:
            /* 菜单开启期间：放行快门对焦/拍照键，拦截其余面板键和转盘以免透传触发相机原界面 ISO/EV */
            if (keycode == 0x15010020u || keycode == 0x15010030u ||
                keycode == 0x15010230u || keycode == 0x15010290u) {
                return ZVP_ACT_NONE;
            }
            if ((keycode & 0xFFFF0000u) == 0x15010000u ||
                (keycode & 0xFFFF0000u) == 0x15020000u ||
                (keycode & 0xFFFF0000u) == 0x15030000u) {
                return ZVP_ACT_CONSUME_SYS;
            }
            return ZVP_ACT_NONE;
    }
}

uint32_t zvp_input_tick(zvp_input_state_t *st, uint64_t now_ms)
{
    if (!st) {
        return ZVP_ACT_NONE;
    }
    if (st->c1_mode == ZVP_C1_MODE_LONG_PRESS &&
        st->c1_is_down && !st->c1_long_fired &&
        now_ms >= st->c1_press_ms &&
        (now_ms - st->c1_press_ms) >= ZVP_C1_LONG_PRESS_MS) {
        st->c1_long_fired = true;
        st->menu_visible = !st->menu_visible;
        return ZVP_ACT_TOGGLE_MENU | ZVP_ACT_CONSUME_SYS;
    }
    return ZVP_ACT_NONE;
}

bool zvp_input_is_dial_keycode(uint32_t keycode)
{
    return keycode == ZVP_KEY_DIAL_TOP ||
           keycode == ZVP_KEY_DIAL_WHEEL ||
           keycode == ZVP_KEY_DIAL_TOP_R180 ||
           keycode == ZVP_KEY_DIAL_WHEEL_R180;
}

uint32_t zvp_input_process_dial(zvp_input_state_t *st,
                                uint32_t keycode,
                                int32_t delta)
{
    if (!st || !st->menu_visible || !zvp_input_is_dial_keycode(keycode)) {
        return ZVP_ACT_NONE;
    }
    if (delta > 0) {
        return ZVP_ACT_DIAL_NEXT | ZVP_ACT_CONSUME_SYS;
    }
    if (delta < 0) {
        return ZVP_ACT_DIAL_PREV | ZVP_ACT_CONSUME_SYS;
    }
    return ZVP_ACT_CONSUME_SYS;
}

static int16_t clamp_i16(int16_t v, int16_t lo, int16_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void zvp_input_set_mini_widget(zvp_input_state_t *st,
                               bool visible,
                               int16_t x,
                               int16_t y,
                               int16_t w,
                               int16_t h)
{
    if (!st) {
        return;
    }
    st->mini_widget_visible = visible;
    st->mini_x = x;
    st->mini_y = y;
    st->mini_w = w;
    st->mini_h = h;
    if (!visible) {
        st->mini_touch_active = false;
        st->mini_suppress_release = false;
    }
}

bool zvp_input_point_in_mini(const zvp_input_state_t *st, int16_t x, int16_t y)
{
    if (!st || !st->mini_widget_visible || st->mini_w <= 0 || st->mini_h <= 0) {
        return false;
    }
    return x >= st->mini_x && x < (int16_t)(st->mini_x + st->mini_w) &&
           y >= st->mini_y && y < (int16_t)(st->mini_y + st->mini_h);
}

uint32_t zvp_input_process_touch(zvp_input_state_t *st,
                                 int16_t raw_x,
                                 int16_t raw_y,
                                 bool is_down)
{
    if (!st) {
        return ZVP_ACT_NONE;
    }

    if (!st->menu_visible && !st->mini_widget_visible) {
        if (!is_down) {
            st->touch_pressed = false;
            st->mini_touch_active = false;
            st->mini_suppress_release = false;
        }
        return ZVP_ACT_NONE;
    }

    int16_t x = clamp_i16(raw_x, 0, ZVP_SCREEN_WIDTH - 1);
    int16_t y = clamp_i16(raw_y, 0, ZVP_SCREEN_HEIGHT - 1);

    if (zvp_input_is_ui_rotated_180(st)) {
        x = (int16_t)((ZVP_SCREEN_WIDTH - 1) - x);
        y = (int16_t)((ZVP_SCREEN_HEIGHT - 1) - y);
    }

    if (!st->menu_visible) {
        /* 仅拦截落入左上角小悬浮窗矩形内的触控，其余区域全部透传给相机对焦/追踪 */
        if (is_down) {
            bool first_down = !st->touch_pressed;
            bool in_rect = zvp_input_point_in_mini(st, x, y);
            if (first_down && !in_rect) {
                st->mini_touch_active = false;
                st->mini_suppress_release = false;
                return ZVP_ACT_NONE;
            }
            if (!st->mini_touch_active && !in_rect) {
                return ZVP_ACT_NONE;
            }
            uint32_t act = ZVP_ACT_CONSUME_SYS |
                           (st->mini_touch_active ? ZVP_ACT_TOUCH_MOVE : ZVP_ACT_TOUCH_DOWN);
            st->mini_touch_active = true;
            st->mini_suppress_release = true;
            st->touch_pressed = true;
            st->touch_x = x;
            st->touch_y = y;
            return act;
        } else {
            st->touch_pressed = false;
            if (!st->mini_touch_active) {
                return ZVP_ACT_NONE;
            }
            st->mini_touch_active = false;
            st->touch_x = x;
            st->touch_y = y;
            return ZVP_ACT_CONSUME_SYS | ZVP_ACT_TOUCH_UP;
        }
    }

    st->mini_touch_active = false;
    st->touch_x = x;
    st->touch_y = y;

    uint32_t act = ZVP_ACT_CONSUME_SYS;
    if (is_down) {
        act |= st->touch_pressed ? ZVP_ACT_TOUCH_MOVE : ZVP_ACT_TOUCH_DOWN;
        st->touch_pressed = true;
    } else {
        st->touch_pressed = false;
        act |= ZVP_ACT_TOUCH_UP;
    }
    return act;
}
