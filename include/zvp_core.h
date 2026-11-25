#ifndef ZVP_CORE_H
#define ZVP_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 1. 卡控开机门禁与熔断模块 (zvp_gate.c)
 * ========================================================================== */

typedef enum {
    ZVP_GATE_ALLOW = 0,
    ZVP_GATE_DISABLED_NO_CARD = 1,
    ZVP_GATE_DISABLED_KILL_FILE = 2,
    ZVP_GATE_DISABLED_UNINSTALL = 3
} zvp_gate_status_t;

typedef struct zvp_gate_ops {
    uint32_t (*get_mount_drives)(void);
    int      (*file_exists)(const char *path);
    void     (*sleep_ms)(uint32_t ms);
} zvp_gate_ops_t;

zvp_gate_status_t zvp_gate_evaluate_boot(const zvp_gate_ops_t *ops,
                                         uint32_t timeout_ms,
                                         uint32_t poll_interval_ms);
const char *zvp_gate_status_str(zvp_gate_status_t st);

/* ============================================================================
 * 2. 5GHz Wi-Fi 频段解锁与全信道管理 (zvp_wifi5g.c)
 * ========================================================================== */

#define ZVP_WIFI5G_CHANNEL_COUNT 10 /* Auto + 9 个白名单信道 */

typedef enum {
    ZVP_PATCH_IDLE = 0,
    ZVP_PATCH_WAIT_NETSVC = 1, /* 开关已开，等待 libNetService.so 懒加载后补丁 */
    ZVP_PATCH_ACTIVE = 2,
    ZVP_PATCH_ERROR = 3
} zvp_wifi5g_state_t;

typedef struct zvp_wifi5g_platform_ops {
    uintptr_t (*find_module_base)(const char *soname);
    int       (*read_mem)(uintptr_t addr, void *out_buf, size_t len);
    int       (*write_mem)(uintptr_t addr, const void *buf, size_t len);
    int       (*backup_read)(uint32_t bkid, void *buf);
    int       (*backup_write)(uint32_t subsys, uint32_t bkid, const void *buf);
} zvp_wifi5g_platform_ops_t;

typedef struct zvp_wifi5g_ctx {
    zvp_wifi5g_state_t state;
    bool enabled;
    uint8_t selected_channel; /* 0xFF = Auto, 或 36/40/44/48/149/153/157/161/165 */
    bool net_patched;
    bool caution_patched;
    bool backup_initialized;
    uint8_t orig_netsvc[4];
    uint8_t orig_caution;
    uint8_t orig_enable_bk;
    uint8_t orig_nvram_go;
    uint8_t orig_nvram_sta;
    uint8_t orig_band_menu;
    uint8_t orig_ap_ch_band[2];
    char last_msg[96];
    zvp_wifi5g_platform_ops_t ops;
} zvp_wifi5g_ctx_t;

void zvp_wifi5g_init(zvp_wifi5g_ctx_t *ctx, const zvp_wifi5g_platform_ops_t *ops);
const uint8_t *zvp_wifi5g_get_channels(size_t *out_count);
bool zvp_wifi5g_is_valid_channel(uint8_t ch);
int zvp_wifi5g_set_enabled(zvp_wifi5g_ctx_t *ctx, bool enable);
int zvp_wifi5g_select_channel(zvp_wifi5g_ctx_t *ctx, uint8_t ch);
void zvp_wifi5g_poll(zvp_wifi5g_ctx_t *ctx);

/* ============================================================================
 * 3. 安全复位模块 (zvp_recovery.c)
 * ========================================================================== */

#define ZVP_RECOVERY_ARM_TIMEOUT_MS 3000u

typedef enum {
    ZVP_REC_IDLE = 0,
    ZVP_REC_ARMED = 1,
    ZVP_REC_DONE = 2,
    ZVP_REC_FAILED = 3
} zvp_recovery_phase_t;

typedef int (*zvp_recovery_extra_fn)(void *user_data);

typedef struct zvp_recovery_ctx {
    zvp_recovery_phase_t phase;
    uint64_t armed_at_ms;
    char status_text[96];
    zvp_recovery_extra_fn full_reset_cb;
    void *full_reset_user;
} zvp_recovery_ctx_t;

void zvp_recovery_init(zvp_recovery_ctx_t *rec);
void zvp_recovery_set_full_reset_cb(zvp_recovery_ctx_t *rec,
                                    zvp_recovery_extra_fn cb,
                                    void *user_data);
void zvp_recovery_tick(zvp_recovery_ctx_t *rec, uint64_t now_ms);
/* 首次调用进入 ARMED，3 秒内二次调用执行全量原子回滚 */
zvp_recovery_phase_t zvp_recovery_trigger(zvp_recovery_ctx_t *rec,
                                          zvp_wifi5g_ctx_t *wifi,
                                          uint64_t now_ms);

/* 按备份的原厂参数还原 Sony Backup (GO/STA NVRAM、频段菜单、AP 频段) */
int zvp_recovery_restore_backup_vals(const zvp_wifi5g_platform_ops_t *ops,
                                     bool backup_initialized,
                                     uint8_t orig_nvram_go,
                                     uint8_t orig_nvram_sta,
                                     uint8_t orig_band_menu,
                                     const uint8_t orig_ap_ch_band[2]);

/* 开机 Boot-Loop 防重启看门狗：连续崩溃 >= max_fails 次后自动还原 mode3 并清 preload */
typedef struct zvp_boot_guard_ops {
    int (*read_file)(const char *path, void *buf, size_t cap, size_t *out_len);
    int (*write_file)(const char *path, const void *buf, size_t len);
    int (*unlink_file)(const char *path);
} zvp_boot_guard_ops_t;

typedef struct zvp_boot_guard_state {
    uint32_t magic;
    uint32_t version;
    uint32_t fail_count;
    uint32_t trip_count;
} zvp_boot_guard_state_t;

bool zvp_boot_guard_disable_preload(const zvp_boot_guard_ops_t *ops,
                                    const char *dmode_path,
                                    const char *preload_path);
bool zvp_boot_guard_enter(const zvp_boot_guard_ops_t *ops,
                          const char *guard_path,
                          const char *dmode_path,
                          const char *preload_path,
                          uint32_t max_fails,
                          zvp_boot_guard_state_t *out_state);
void zvp_boot_guard_mark_healthy(const zvp_boot_guard_ops_t *ops,
                                 const char *guard_path);

/* ============================================================================
 * 4. C1 按键策略、触控隔离与 180° 翻转屏自适应 (zvp_input_hook.c)
 * ========================================================================== */

#define ZVP_C1_LONG_PRESS_MS 500u

typedef enum {
    ZVP_C1_MODE_LONG_PRESS = 0,     /* 默认：长按 >=500ms 开关菜单；短按透传给相机原功能 */
    ZVP_C1_MODE_SHORT_EXCLUSIVE = 1 /* 短按独占：立刻切换菜单并拦截相机原功能 */
} zvp_c1_mode_t;

typedef enum {
    ZVP_ACT_NONE = 0,
    ZVP_ACT_TOGGLE_MENU = 1 << 0,
    ZVP_ACT_NAV_UP      = 1 << 1,
    ZVP_ACT_NAV_DOWN    = 1 << 2,
    ZVP_ACT_NAV_LEFT    = 1 << 3,
    ZVP_ACT_NAV_RIGHT   = 1 << 4,
    ZVP_ACT_NAV_ENTER   = 1 << 5,
    ZVP_ACT_CLOSE_MENU  = 1 << 6,
    ZVP_ACT_TOUCH_DOWN  = 1 << 7,
    ZVP_ACT_TOUCH_MOVE  = 1 << 8,
    ZVP_ACT_TOUCH_UP    = 1 << 9,
    ZVP_ACT_CONSUME_SYS = 1 << 10, /* 置位表示吞掉该系统事件，不转发给 RTLD_NEXT */
    ZVP_ACT_DIAL_NEXT   = 1 << 11, /* 滚轮顺时针（向下/向右切换） */
    ZVP_ACT_DIAL_PREV   = 1 << 12  /* 滚轮逆时针（向上/向左切换） */
} zvp_input_action_t;

typedef struct zvp_input_state {
    zvp_c1_mode_t c1_mode;
    bool menu_visible;
    bool auto_flip;
    bool hw_flipped;
    uint32_t c1_keycode;

    /* 长按判别状态机 */
    bool c1_is_down;
    bool c1_long_fired;
    uint64_t c1_press_ms;

    /* 最近触控逻辑坐标 (0..639, 0..479，已根据翻转状态换算) */
    int16_t touch_x;
    int16_t touch_y;
    bool touch_pressed;

    /* 拍摄界面左上角固定小悬浮窗局部触摸热区（主菜单关闭时生效） */
    bool mini_widget_visible;
    int16_t mini_x;
    int16_t mini_y;
    int16_t mini_w;
    int16_t mini_h;
    bool mini_touch_active;
    bool mini_suppress_release;
} zvp_input_state_t;

void zvp_input_init(zvp_input_state_t *st, uint32_t c1_keycode);
void zvp_input_set_c1_mode(zvp_input_state_t *st, zvp_c1_mode_t mode);
void zvp_input_set_auto_flip(zvp_input_state_t *st, bool enable);
void zvp_input_update_flip(zvp_input_state_t *st, bool is_flipped);
bool zvp_input_is_ui_rotated_180(const zvp_input_state_t *st);
void zvp_input_set_mini_widget(zvp_input_state_t *st,
                               bool visible,
                               int16_t x,
                               int16_t y,
                               int16_t w,
                               int16_t h);
bool zvp_input_point_in_mini(const zvp_input_state_t *st, int16_t x, int16_t y);

/* 返回 ZVP_ACT_* 组合掩码；若包含 ZVP_ACT_TOGGLE_MENU 则需要切换菜单显隐 */
uint32_t zvp_input_process_key(zvp_input_state_t *st,
                               uint32_t keycode,
                               bool is_press,
                               uint64_t now_ms);
bool zvp_input_is_dial_keycode(uint32_t keycode);
uint32_t zvp_input_process_dial(zvp_input_state_t *st,
                                uint32_t keycode,
                                int32_t delta);
uint32_t zvp_input_tick(zvp_input_state_t *st, uint64_t now_ms);
uint32_t zvp_input_process_touch(zvp_input_state_t *st,
                                 int16_t raw_x,
                                 int16_t raw_y,
                                 bool is_down);

/* 全局上下文访问与跨模块桥梁（供 UI、LVGL port 与入口层协调） */
void zvp_logf(const char *fmt, ...);
uint64_t zvp_monotonic_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* ZVP_CORE_H */
