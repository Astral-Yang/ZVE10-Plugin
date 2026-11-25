#ifndef ZVP_RUNTIME_H
#define ZVP_RUNTIME_H

#include "zvp_core.h"
#include "zvp_abi.h"
#include "zvp_sound.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Sony 原生画布桥接状态 (zvp_native_bridge.c)
 * ========================================================================== */
typedef struct zvp_native_bridge {
    bool symbols_resolved;
    bool ticker_ready;
    bool ticker_failed;
    bool canvas_ready;
    bool canvas_failed;
    uint32_t anim_ticks;

    void *lib_obj;
    void *ticker_layer;
    void *ticker_widget;
    uint32_t ticker_layer_id;

    void *layer_obj;
    void *canvas_widget; /* 分配 0x400 字节的 GEN_CacheInsImg 实例 */
    void *bmp_obj;       /* grm::bmp::BitmapImpl* */
    uint32_t layer_id;
    uint32_t res_id;

    /* 克隆的 vtable（含前 2 个 RTTI/offset-to-top 负偏移槽）用于挂接常驻 ticker Widget::onAnimate */
    uintptr_t cloned_vtable[66];
    int on_animate_slot;
    void (*orig_on_animate)(void *self);
} zvp_native_bridge_t;

int zvp_bridge_init_platform(zvp_wifi5g_platform_ops_t *out_wifi_ops,
                             zvp_gate_ops_t *out_gate_ops);
int zvp_bridge_ensure_ticker(zvp_native_bridge_t *br);
void zvp_bridge_stop_ticker(zvp_native_bridge_t *br);
int zvp_bridge_ensure_canvas(zvp_native_bridge_t *br);
void zvp_bridge_set_overlay_visible(zvp_native_bridge_t *br, bool visible);
void zvp_bridge_blit_argb8888(zvp_native_bridge_t *br,
                              int32_t x1, int32_t y1,
                              int32_t x2, int32_t y2,
                              const uint32_t *argb_pixels,
                              int32_t buf_stride_px,
                              bool rot180);
void zvp_bridge_commit_sprite(zvp_native_bridge_t *br);
void zvp_bridge_clear_canvas(zvp_native_bridge_t *br);

/* ============================================================================
 * LVGL 渲染驱动与输入绑定 (zvp_lvgl_port.c)
 * ========================================================================== */
typedef struct zvp_lvgl_port {
    bool initialized;
    lv_display_t *disp;
    lv_indev_t *indev_touch;
    lv_indev_t *indev_keypad;
    uint32_t pending_key;
    bool pending_key_pressed;
    int16_t cur_touch_x;
    int16_t cur_touch_y;
    bool cur_touch_pressed;
    uint32_t last_tick_ms;
    /* 双全屏或行缓冲：此处使用 640x80 行 ARGB8888 局部缓冲 (~200KB) 节省相机内存 */
    uint32_t draw_buf[ZVP_SCREEN_WIDTH * 80u];
} zvp_lvgl_port_t;

int zvp_lvgl_port_init(zvp_lvgl_port_t *port, zvp_native_bridge_t *br);
void zvp_lvgl_port_tick_and_render(zvp_lvgl_port_t *port);
void zvp_lvgl_port_push_key(zvp_lvgl_port_t *port, uint32_t lv_key);

/* ============================================================================
 * 单色极简全透明 UI 模块 (zvp_ui.c)
 * ========================================================================== */
#define ZVP_MINI_WIDGET_X 10
#define ZVP_MINI_WIDGET_Y 10
#define ZVP_MINI_WIDGET_W 252
#define ZVP_MINI_WIDGET_H 46

typedef enum {
    ZVP_PAGE_CREATIVE_LOOK = 0,
    ZVP_PAGE_LUT = 1,
    ZVP_PAGE_WIFI5G = 2,
    ZVP_PAGE_FILE_SHARE = 3,
    ZVP_PAGE_CUSTOM_SHUTTER = 4,
    ZVP_PAGE_COLOR_CAL = 5,
    ZVP_PAGE_ASPECT_RATIO = 6,
    ZVP_PAGE_USB_REC = 7,
    ZVP_PAGE_1080P_OVERSAMPLE = 8,
    ZVP_PAGE_SETTINGS = 9,
    ZVP_PAGE_COUNT
} zvp_ui_page_t;

typedef struct zvp_ui {
    bool built;
    zvp_ui_page_t active_page;
    lv_group_t *input_group;
    lv_obj_t *focused_obj;

    lv_obj_t *scr;
    lv_obj_t *left_nav;
    lv_obj_t *nav_list;
    lv_obj_t *nav_btns[ZVP_PAGE_COUNT];
    lv_obj_t *lbl_nav_hint;
    lv_obj_t *right_panel;
    lv_obj_t *pages[ZVP_PAGE_COUNT];

    /* 5GHz Wi-Fi 页控件 */
    lv_obj_t *btn_wifi_card;
    lv_obj_t *badge_wifi5g;
    lv_obj_t *lbl_wifi5g_badge;
    lv_obj_t *lbl_wifi_status;
    lv_obj_t *ch_grid;
    lv_obj_t *ch_btns[ZVP_WIFI5G_CHANNEL_COUNT];

    /* 自定义快门音页控件 */
    lv_obj_t *btn_shutter_enable;
    lv_obj_t *badge_shutter_enable;
    lv_obj_t *lbl_shutter_badge;
    lv_obj_t *lbl_shutter_status;
    lv_obj_t *btn_mini_enable;
    lv_obj_t *badge_mini_enable;
    lv_obj_t *lbl_mini_badge;
    lv_obj_t *btn_sound_card;
    lv_obj_t *btn_sound_prev;
    lv_obj_t *btn_sound_next;
    lv_obj_t *lbl_sound_name;
    lv_obj_t *lbl_sound_src;
    lv_obj_t *btn_sound_test;
    lv_obj_t *lbl_sound_test;

    /* 拍摄界面左上角固定小悬浮窗控件 */
    lv_obj_t *mini_win;
    lv_obj_t *btn_mini_prev;
    lv_obj_t *btn_mini_play;
    lv_obj_t *lbl_mini_play;
    lv_obj_t *btn_mini_next;

    /* 设置与安全页控件 */
    lv_obj_t *btn_c1_mode;
    lv_obj_t *badge_c1_mode;
    lv_obj_t *lbl_c1_mode;
    lv_obj_t *btn_flip_card;
    lv_obj_t *badge_auto_flip;
    lv_obj_t *lbl_auto_flip_badge;
    lv_obj_t *lbl_flip_state;
    lv_obj_t *btn_recovery;
    lv_obj_t *lbl_recovery;
    lv_obj_t *lbl_gate_info;
} zvp_ui_t;

void zvp_ui_build(zvp_ui_t *ui);
void zvp_ui_refresh(zvp_ui_t *ui);
void zvp_ui_switch_page(zvp_ui_t *ui, zvp_ui_page_t page);
void zvp_ui_set_visible(zvp_ui_t *ui, bool visible);
void zvp_ui_sync_overlay(zvp_ui_t *ui);
bool zvp_ui_handle_nav_key(zvp_ui_t *ui, uint32_t act_mask);

/* ============================================================================
 * 全局运行时入口容器 (zvp_entry.c)
 * ========================================================================== */
#define ZVP_ACT_QUEUE_SIZE   32u
#define ZVP_TOUCH_QUEUE_SIZE 64u
#define ZVP_KEYLOG_QUEUE_SIZE 16u

typedef struct zvp_touch_sample {
    int16_t raw_x;
    int16_t raw_y;
    int16_t x;
    int16_t y;
    bool pressed;
} zvp_touch_sample_t;

typedef struct zvp_keylog_sample {
    uint32_t w0;
    uint32_t w2;
    bool consumed;
} zvp_keylog_sample_t;

typedef struct zvp_runtime {
    bool boot_evaluated;
    bool boot_tripped;
    bool guard_cleared;
    bool plugin_active;
    bool shutting_down;
    uint64_t boot_start_ms;
    uint64_t active_since_ms;
    uint64_t last_anim_ms;
    long ui_tid;
    zvp_gate_status_t gate_status;

    /* 输入线程 -> UI 主线程单生产者单消费者无锁队列 */
    volatile uint32_t act_queue[ZVP_ACT_QUEUE_SIZE];
    volatile uint32_t q_head;
    volatile uint32_t q_tail;
    volatile zvp_touch_sample_t touch_queue[ZVP_TOUCH_QUEUE_SIZE];
    volatile uint32_t tp_head;
    volatile uint32_t tp_tail;
    volatile zvp_keylog_sample_t keylog_queue[ZVP_KEYLOG_QUEUE_SIZE];
    volatile uint32_t kl_head;
    volatile uint32_t kl_tail;
    volatile bool touch_dirty;
    volatile bool flip_dirty;
    int16_t diag_last_tp_x;
    int16_t diag_last_tp_y;
    uint32_t diag_last_key_w0;

    zvp_wifi5g_ctx_t wifi;
    zvp_recovery_ctx_t rec;
    zvp_sound_mgr_t sound;
    bool shutter_sound_enabled;
    bool mini_widget_enabled;
    uint64_t last_shutter_trig_ms;
    zvp_input_state_t input;
    zvp_native_bridge_t bridge;
    zvp_lvgl_port_t lvgl;
    zvp_ui_t ui;
} zvp_runtime_t;

zvp_runtime_t *zvp_runtime_get(void);
bool zvp_runtime_is_bypassed(void);
void zvp_runtime_save_prefs(void);
void zvp_runtime_reset_prefs(void);
void zvp_runtime_ui_frame_tick(void);
void zvp_runtime_on_animate(void);
void zvp_runtime_handle_action_mask(uint32_t act_mask);

#ifdef __cplusplus
}
#endif

#endif /* ZVP_RUNTIME_H */
